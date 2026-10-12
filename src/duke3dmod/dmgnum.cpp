#include "dmgnum.h"
#include "wallclock.h"
#include "hooks.h"
#include "engine.h"
#include "project.h"
#include "esp_layout.h"
#include "target.h"
// aim_last_shot names the shot a hit pairs with; actorstate.h holds the shared line-budget rule.
#include "aim.h"
#include "actorstate.h"
#include "input.h"
#include "canvas.h"
#include "config.h"
#include "font.h"
#include "log.h"
#include <stdio.h>
#include <windows.h>

// Floating damage numbers: hooks ifhitbyweapon, draws from dmgnum.h's pure rules; no engine write. The hook runs on the tic
// clock and only writes s_ring; dmgnum_draw reads it on the render clock. Reads happen before the original call, which
// clears hittype[i].extra to -1 and may retire a killed sprite.

typedef short (__cdecl* IfHitByWeaponFn)(short);
static IfHitByWeaponFn s_orig = 0;
static bool s_installed = false;
static DmgRing s_ring;

static int hittype_s(int i, unsigned off) {
  return (int)(short)read_field_bytes(g_base, rva::RVA_HITTYPE + (unsigned)i * rva::HITTYPE_STRIDE + off, 2);
}

static int victim_upper_z(int i) {
  const int z    = engine_sprite_field_l(i, SPR_Z);
  const int pic  = engine_sprite_field_s(i, SPR_PICNUM);
  const int yrep = engine_sprite_field_u8(i, SPR_YREPEAT);
  return target_aim_z(z, yrep, engine_tilesizy(pic), AP_UPPER);
}

const unsigned DMGNUM_DIAG_WINDOW_MS = 3000;
static bool diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, DMGNUM_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

static int  s_diagLastDamage = 0;
static bool s_diagLastKill   = false;

// 300, matching aim.cpp's shot instrument: hit and shot lines are read as pairs, so one side's budget running out reads as misses.
const int DMGNUM_HIT_MAX_LINES = 300;
static int s_hitLines = 0;

void dmgnum_hit_budget_reset() {
  s_hitLines = 0;
}

// The original is always called: health must drop whether or not the feature is on.
static short __cdecl hooked_ifhitbyweapon(short i) {
  if (!s_orig) return -1;
  if (!g_cfg.dmgnumbers && g_cfg.debuglog == 0) return s_orig(i);
  const bool readable = engine_verified() && i >= 0 && i < rva::MAX_SPRITES;
  const bool wantInstrument = g_cfg.debuglog != 0 && readable &&
                              actorstate_budget_ok(s_hitLines, DMGNUM_HIT_MAX_LINES);
  if (!g_cfg.dmgnumbers && !wantInstrument) return s_orig(i);
  if (!readable) return s_orig(i);

  const int damage = hittype_s(i, rva::HITTYPE_EXTRA);
  const int owner  = hittype_s(i, rva::HITTYPE_OWNER);
  const int hpBefore = engine_sprite_field_s(i, SPR_EXTRA);
  // ps[myconnectindex].i, the local player's sprite; unresolved leaves mySprite -1, which the filter refuses.
  unsigned psRva = 0; int mySprite = -1;
  if (camera_ps_rva(engine_myconnectindex(), &psRva))
    mySprite = (int)(short)read_field_bytes(g_base, psRva + rva::PS_I, 2);

  // Hit line, before the dmgnumbers gate on purpose (an instrument). Pairs with aim.cpp's `aim shot:` line by shot number
  // and ms; no hit within ~70 ms (two tics) reads as a miss. `damage != -1` drops per-tic polls of healthy actors (869 of
  // 900 unfiltered lines).
  if (wantInstrument && damage != -1 && mySprite >= 0 && owner == mySprite) {
    s_hitLines++;
    unsigned shotNo = 0, shotMs = 0;
    aim_last_shot(&shotNo, &shotMs, 0);
    LOGI("aim hit: target=%d pic=%d damage=%d extra=%d cstat=0x%04X shot=#%u dt=%ums tic=%u",
         (int)i, engine_sprite_field_s(i, SPR_PICNUM), damage, hpBefore,
         (unsigned)(engine_sprite_field_s(i, SPR_CSTAT) & 0xFFFF),
         shotNo, shotNo ? wallclock_ms() - shotMs : 0u, input_tic_count());
  }

  if (!g_cfg.dmgnumbers) return s_orig(i);

  // Liveness is asked before the original: after it a killed actor's `extra` is negative, hiding the killing hit.
  const bool liveEnemy = actor_state_targetable(actor_state_cached(i));
  const bool wanted = dmgnum_event_wanted(owner, mySprite, liveEnemy, damage);

  int x = 0, y = 0, z = 0;
  if (wanted) {
    x = engine_sprite_field_l(i, SPR_X);
    y = engine_sprite_field_l(i, SPR_Y);
    z = victim_upper_z(i);
  }

  const short ret = s_orig(i);

  if (wanted) {
    const int hpAfter = engine_sprite_field_s(i, SPR_EXTRA);
    DmgEvent ev = { x, y, z, damage, hpBefore > 0 && hpAfter <= 0, i, wallclock_ms(), true };
    dmgnum_push(&s_ring, ev);
    s_diagLastDamage = damage;
    s_diagLastKill   = ev.kill;
  }
  return ret;
}

// Ring order (oldest first) matches dmgnum_stack_index. Reuses the ESP's refusals (camera validity, gproj, dist2d cull,
// not depth, which moves with FOV) so a number never disagrees with its enemy marker about being on screen.
void dmgnum_draw(int screenW, int screenH) {
  if (!g_cfg.dmgnumbers || !s_installed || !engine_verified()) return;

  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);

  const unsigned now = wallclock_ms();
  const int textH = font_height[FONT_UI];
  int live = 0;
  int drawn = 0;

  for (int k = 0; k < DMGNUM_MAX; k++) {
    const int slot = (s_ring.next + k) % DMGNUM_MAX;
    const DmgEvent& e = s_ring.e[slot];
    if (!dmgnum_is_live(e, now)) continue;
    live++;

    const ScreenPt p = project_world(cam, g, sintable, e.x, e.y, e.z);
    if (!p.visible) continue;
    if (!esp_marker_in_range(p, g_cfg.espdrawdist)) continue;
    if (!proj_on_screen(p.x, p.y, screenW, screenH)) continue;
    drawn++;

    char s[16];
    _snprintf_s(s, sizeof(s), _TRUNCATE, "%d", e.damage);
    const double pr = dmgnum_progress(e.t0, now);
    const int rise = dmgnum_rise_px(pr, textH) + dmgnum_stack_index(s_ring, slot, now) * textH;
    canvas_text(p.x - canvas_text_width(s, FONT_UI) / 2, p.y - rise, s,
                canvas_with_alpha(dmgnum_colour(e.kill, canvas_esp_colour(true), C_HAZARD),
                                  dmgnum_alpha(pr)),
                FONT_UI);
  }

  if (g_cfg.debuglog != 0 && diag_due())
    LOGI("dmgnum: live=%d drawn=%d last=%d kill=%d", live, drawn, s_diagLastDamage, s_diagLastKill ? 1 : 0);
}

// One of twelve thunk fingerprint checks feeding engine.h's aggregate. A failure is never fatal: s_orig stays null and
// dmgnum_draw refuses on s_installed.

bool dmgnum_install() {
  dmgnum_reset(&s_ring);
  void* t = 0;
  const bool ok = hook_jmp_thunk(rva::THUNK_IFHITBYWEAPON_RVA, rva::THUNK_IFHITBYWEAPON_BYTES,
                                 (void*)&hooked_ifhitbyweapon, &t);
  engine_mark_hook_result(ok);
  if (ok) {
    s_orig = (IfHitByWeaponFn)t;
    const unsigned origRva = (unsigned)((unsigned char*)t - g_base);
    LOGI("dmgnum: ifhitbyweapon thunk hook installed; recovered original target rva=%08X (expect %08X)%s",
         origRva, rva::IFHITBYWEAPON_BODY_RVA,
         origRva == rva::IFHITBYWEAPON_BODY_RVA ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("dmgnum: ifhitbyweapon thunk hook failed; damage numbers are off for this session");
  }
  s_installed = ok;
  return s_installed;
}
