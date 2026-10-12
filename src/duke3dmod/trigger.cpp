#include "trigger.h"
#include "wallclock.h"
#include "engine.h"
#include "esp_world.h"
#include "esp_switch.h"
#include "esp_layout.h"
#include "target.h"
#include "project.h"
#include "world.h"
#include "canvas.h"
#include "common.h"
#include "hooks.h"
#include "config.h"
#include "font.h"
#include "log.h"
#include <stdio.h>
#include <windows.h>

// The secret trigger ESP: the tic-clock scan and the draw. Decides nothing; every decision is a pure rule in trigger.h.

// Two clocks: trigger_tic runs from hooked_processinput's tail and reads map memory only; trigger_draw runs on the
// render clock. They meet at s_chains, a plain array: tic and frame share one thread, so no seqlock.

// A second, unrelated pair (secret_volume_tic/collect, foot of file) outlines every undiscovered secret sector; it lives
// here because it spends this file's wall staging, curtain and corner rule.

// Statnum 6 is where operatemasterswitches and movestandables' touchplate block look; statnum 8 is operateactivators'.
const int TRIG_STAT_STANDABLE = ESP_STAT_STANDABLE;
const int TRIG_STAT_ACTIVATOR = 8;

// TOUCHPLATE and MASTERSWITCH share statnum 6, told apart by picnum; ACTIVATORLOCKED toggles its sector's LOCK bit.
const int TRIG_PIC_TOUCHPLATE      = 3;
const int TRIG_PIC_MASTERSWITCH    = 8;
const int TRIG_PIC_ACTIVATORLOCKED = 4;

// hittype[i].temp_data[5]: the engine's "this plate is used up" flag, set when a hitag-limited plate's budget runs out.
const int TRIG_TEMP_EXHAUSTED = 5;

// Sprites one scan may capture. A refusal, not a clamp: past it the walk stops.
const int TRIG_SPRITE_MAX = 512;

const unsigned TRIG_DIAG_WINDOW_MS = 3000;

// Half the fallback curtain's width in Build units (512/metre): two metres across, unmissable yet not the room outline.
const int TRIGVOL_CURTAIN_HALF = 512;

// Undiscovered secret sectors one snapshot may stage; secretstosectormap is a short[16], so 32 is a refusal past which
// the walk stops.
const int SECVOL_MAX_SECTORS = 32;

static TrigChain s_chains[TRIG_MAX_CHAINS];
static int       s_count = 0;
static unsigned  s_sig = 0;
static unsigned  s_lastScanMs = 0;
static bool      s_scanned = false;

// Static, not stack: this runs on the tic clock inside the engine's update (as engine.cpp's s_nsWalls/s_nsSectors).
static TrigSprite s_sp[TRIG_SPRITE_MAX];
static TrigSector s_sec[rva::MAX_SECTORS];
static short      s_wallNext[rva::MAX_WALLS];

// The secret volume's snapshot, one group per undiscovered secret (a room with no trigger is staged, one a plate merely
// opens is not); one group per sector, so no dedupe.
static TrigWall s_secWalls[TRIGVOL_WALLS_MAX];
static int      s_secWallCount = 0;
static int      s_secFirst[SECVOL_MAX_SECTORS];      // where room r's group starts
static int      s_secN[SECVOL_MAX_SECTORS];          // its wall count (0: nothing to draw)
static int      s_secSect[SECVOL_MAX_SECTORS];
static int      s_secCx[SECVOL_MAX_SECTORS];         // the room's centre, for the range test
static int      s_secCy[SECVOL_MAX_SECTORS];
static int      s_secCount = 0;                      // groups published
static unsigned s_secSig = 0;
static unsigned s_secLastScanMs = 0;
static bool     s_secScanned = false;
static bool     s_secOn = false;                     // what the switches were when staged

// Fills one TrigSprite's position and tag fields; the role fields are the caller's.
static void trig_fill_common(TrigSprite& s, int i) {
  s.index   = i;
  s.lotag   = engine_sprite_field_s(i, SPR_LOTAG);
  s.hitag   = engine_sprite_field_s(i, SPR_HITAG);
  s.sectnum = engine_sprite_field_s(i, SPR_SECTNUM);
  s.x       = engine_sprite_field_l(i, SPR_X);
  s.y       = engine_sprite_field_l(i, SPR_Y);
  s.z       = engine_sprite_field_l(i, SPR_Z);
}

static bool trig_diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, TRIG_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

void trigger_tic() {
  // Off means no scan and no stale chains: the published set is emptied so turning it on rebuilds next tic.
  if (!g_cfg.secrettriggers || !engine_verified()) {
    s_count = 0;
    s_scanned = false;
    return;
  }

  const int nsectors = engine_numsectors();
  const int nwalls   = engine_numwalls();

  // Standables list length, the third signature term. Finished before any other walk: the engine's cycle counter is
  // single-slot, so walks are sequential.
  int standables = 0;
  for (int i = engine_first_sprite_of_stat(TRIG_STAT_STANDABLE); i >= 0; i = engine_next_sprite(i))
    standables++;

  const unsigned sig = trigger_map_signature(nsectors, nwalls, standables);
  const unsigned now = wallclock_ms();
  // Signature first (a level change is immediate), timer second (found secrets and opened locks go under a second
  // stale); the unsigned difference is exact across the clock's wrap. The volume's switch is not a term.
  if (s_scanned && sig == s_sig && (now - s_lastScanMs) < TRIG_SCAN_PERIOD_MS) return;

  int nsp = 0;
  // The sprite cap is shared across the three walks in LIST ORDER; activators go first so a flood starves the lists a
  // player is less likely to have hundreds of. `overflow` makes starvation visible in the diagnostic.
  bool overflow = false;

  for (int i = engine_first_sprite_of_stat(TRIG_STAT_ACTIVATOR); i >= 0; i = engine_next_sprite(i)) {
    if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
    const int pic = engine_sprite_field_s(i, SPR_PICNUM);
    TrigSprite& s = s_sp[nsp++];
    trig_fill_common(s, i);
    s.kind = -1;
    s.isActivator = 1;
    s.isLocked = (pic == TRIG_PIC_ACTIVATORLOCKED) ? 1 : 0;
    s.exhausted = false;
  }

  // statnum 6: touchplates and master switches. A master switch is an activator: operatemasterswitches starts a
  // countdown and movestandables operates its sector hitag tics later, so it behaves like a delayed activator.
  for (int i = engine_first_sprite_of_stat(TRIG_STAT_STANDABLE); i >= 0; i = engine_next_sprite(i)) {
    const int pic = engine_sprite_field_s(i, SPR_PICNUM);
    if (pic != TRIG_PIC_TOUCHPLATE && pic != TRIG_PIC_MASTERSWITCH) continue;
    if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
    TrigSprite& s = s_sp[nsp++];
    trig_fill_common(s, i);
    if (pic == TRIG_PIC_TOUCHPLATE) {
      s.kind = TRIG_TOUCHPLATE; s.isActivator = 0; s.isLocked = 0;
      // A read that cannot be trusted is NOT "used up": a false return leaves exhausted false.
      int t5 = 0;
      s.exhausted = engine_actor_temp_data(i, TRIG_TEMP_EXHAUSTED, &t5) && t5 == 1;
    } else {
      s.kind = -1; s.isActivator = 1; s.isLocked = 0; s.exhausted = false;
    }
  }

  // statnum 0: the visible switches via switch_classify (esp_switch.h), which knows the two-frame families, multiswitch,
  // combination members and door tiles, and refuses a lotag-0 switch. got_access is sampled once per walk.
  const int gotAccess = engine_player_got_access();
  for (int i = engine_first_sprite_of_stat(ESP_STAT_SWITCH); i >= 0; i = engine_next_sprite(i)) {
    const int pic = engine_sprite_field_s(i, SPR_PICNUM);
    if (switch_base_for(pic) < 0) continue;
    const int pal   = engine_sprite_field_u8(i, SPR_PAL);
    const int hitag = engine_sprite_field_s(i, SPR_HITAG);
    const int lotag = engine_sprite_field_s(i, SPR_LOTAG);
    if (switch_classify(pic, pal, hitag, lotag, gotAccess).family == SW_NONE) continue;
    if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
    TrigSprite& s = s_sp[nsp++];
    trig_fill_common(s, i);
    s.kind = TRIG_SWITCH; s.isActivator = 0; s.isLocked = 0; s.exhausted = false;
  }

  // Bounded by both the live count and the engine's static array (sector_index_ok): a corrupt numsectors must not widen
  // the window.
  const int nsec = (nsectors < (int)rva::MAX_SECTORS) ? nsectors : (int)rva::MAX_SECTORS;
  for (int s = 0; s < nsec; s++) {
    s_sec[s].lotag   = engine_sector_lotag(s);
    s_sec[s].wallptr = engine_sector_field_s(s, SEC_WALLPTR);
    s_sec[s].wallnum = engine_sector_field_s(s, SEC_WALLNUM);
  }
  const int nw = (nwalls < (int)rva::MAX_WALLS) ? nwalls : (int)rva::MAX_WALLS;
  // engine_wall_field_s returns 0 for both "value is 0" and "refused", which would read as "faces sector 0", a real
  // index; seeded to -1, the engine's "no neighbour".
  for (int w = 0; w < nw; w++)
    s_wallNext[w] = (w < nwalls) ? (short)engine_wall_field_s(w, rva::WAL_NEXTSECTOR) : (short)-1;

  // Publish s_count LAST: the render clock reads s_chains under that count.
  const int newCount = trigger_resolve(s_sp, nsp, s_sec, nsec, s_wallNext, nw, s_chains, TRIG_MAX_CHAINS);
  s_sig = sig;
  s_lastScanMs = now;
  s_scanned = true;
  s_count = newCount;

  // Trigger lotags, looked up while s_sp still holds the sprites; inside the throttle so the log-off session pays nothing.
  if (g_cfg.debuglog != 0 && trig_diag_due()) {
    char lotags[TRIG_MAX_CHAINS * 8];
    int at = 0;
    lotags[0] = 0;
    for (int c = 0; c < s_count && at < (int)sizeof(lotags) - 1; c++) {
      int lotag = 0;
      for (int k = 0; k < nsp; k++)
        if (s_sp[k].index == s_chains[c].trigger) { lotag = s_sp[k].lotag; break; }
      const int wrote = _snprintf_s(lotags + at, sizeof(lotags) - at, _TRUNCATE,
                                    c ? ",%d" : "%d", lotag);
      if (wrote < 0) break;
      at += wrote;
    }
    LOGI("trigger: chains=%d sprites=%d overflow=%d lotags=%s sectors=%d walls=%d",
         s_count, nsp, overflow ? 1 : 0, lotags, nsec, nw);
  }
}

// The secret marker's cross at HALF the arm length in the same colour: a touchplate often sits beside the secret's own
// marker, and two full crosses read as a smudge. Thickness unchanged (a one-pixel bar vanishes at distance).
const int TRIG_MARKER_ARM = ESP_SECRET_ARM / 2;
static void trigger_draw_marker(int cx, int cy, unsigned colour) {
  const int span = TRIG_MARKER_ARM * 2 + 1;
  canvas_rect(clamp_ll_to_int((long long)cx - TRIG_MARKER_ARM),
              clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2),
              span, ESP_SECRET_T, colour);
  canvas_rect(clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2),
              clamp_ll_to_int((long long)cy - TRIG_MARKER_ARM),
              ESP_SECRET_T, span, colour);
}

// Range is a WORLD distance (the projected point is zeroed on refusal); a chain counts when EITHER end is in range. The
// line is clipped, not dropped: one refused end walks toward the other, on `visible` not depth. Markers need their own end
// in range and on screen; seen-lists make each draw once per sprite index per frame (reasoning in trigger.h).
static bool trig_seen(int* seen, int& count, int cap, int index) {
  for (int i = 0; i < count; i++) if (seen[i] == index) return true;
  if (count < cap) seen[count++] = index;
  return false;
}

void trigger_draw(int screenW, int screenH) {
  if (!g_cfg.secrettriggers || !engine_verified() || s_count <= 0) return;

  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  const unsigned colour = canvas_esp_secret_colour();
  const double plane = PROJ_NEAR_CLIP + TRIG_CLIP_MARGIN;

  int trigSeen[TRIG_MAX_CHAINS];  int trigSeenCount = 0;
  int actSeen[TRIG_MAX_CHAINS];   int actSeenCount = 0;

  // The render loop's own bound beside the published count.
  for (int c = 0; c < s_count && c < TRIG_MAX_CHAINS; c++) {
    const TrigChain& ch = s_chains[c];

    const bool tRange = trigger_end_in_range(
        trigger_world_dist2d(cam.posx, cam.posy, ch.tx, ch.ty), g_cfg.espdrawdist);
    const bool aRange = trigger_end_in_range(
        trigger_world_dist2d(cam.posx, cam.posy, ch.ax, ch.ay), g_cfg.espdrawdist);
    if (!tRange && !aRange) continue;

    ProjectDebug tDbg, aDbg;
    const ScreenPt t = project_world(cam, g, sintable, ch.tx, ch.ty, ch.tz, &tDbg);
    const ScreenPt a = project_world(cam, g, sintable, ch.ax, ch.ay, ch.az, &aDbg);

    const bool tOn = tRange && t.visible && proj_on_screen(t.x, t.y, screenW, screenH);
    const bool aOn = aRange && a.visible && proj_on_screen(a.x, a.y, screenW, screenH);

    // The touchplate's own marker and instruction; a switch gets neither ("Switches and locks" draws its own).
    if (ch.kind == TRIG_TOUCHPLATE && tOn && !trig_seen(trigSeen, trigSeenCount, TRIG_MAX_CHAINS, ch.trigger)) {
      trigger_draw_marker(t.x, t.y, colour);
      const char* step = trigger_step_label();
      canvas_text(esp_label_x(t.x, 0, canvas_text_width(step, FONT_SMALL)),
                  esp_secret_dist_label_y(t.y), step, colour, FONT_SMALL);
    }

    ScreenPt e0 = t, e1 = a;
    bool haveLine = t.visible && a.visible;
    if (!haveLine && (t.visible || a.visible)) {
      // The parameter runs trigger end (0) to activator end (1); uKeep is the end known to project, uEnd the refused one.
      const bool tRefused = !t.visible;
      const double uKeep = tRefused ? 1.0 : 0.0;
      const double uEnd  = tRefused ? 0.0 : 1.0;
      // First guess: the near-plane crossing when depths say that is the refusal; otherwise the pitch plane, from the midpoint.
      const double nearT = trigger_segment_clip_t(tDbg.gdepth, aDbg.gdepth, plane);
      const double refusedDepth = tRefused ? tDbg.gdepth : aDbg.gdepth;
      double u = (nearT > 0.0 && nearT < 1.0 && !(refusedDepth >= plane))
                   ? nearT : trigger_bisect_toward(uEnd, uKeep);
      // Halves toward the projecting end each pass, at most TRIG_CLIP_STEPS times.
      for (int k = 0; k <= TRIG_CLIP_STEPS; k++) {
        if (k) u = trigger_bisect_toward(u, uKeep);
        int cx = 0, cy = 0, cz = 0;
        trigger_lerp_point(ch.tx, ch.ty, ch.tz, ch.ax, ch.ay, ch.az, u, &cx, &cy, &cz);
        const ScreenPt cp = project_world(cam, g, sintable, cx, cy, cz);
        if (cp.visible) {
          if (tRefused) e0 = cp; else e1 = cp;
          haveLine = true;
          break;
        }
      }
    }
    if (haveLine) canvas_line(e0.x, e0.y, e1.x, e1.y, colour);

    if (aOn && !trig_seen(actSeen, actSeenCount, TRIG_MAX_CHAINS, ch.activator)) {
      const char* lab = trigger_effect_label(ch.effect);
      canvas_text(esp_label_x(a.x, 0, canvas_text_width(lab, FONT_SMALL)),
                  esp_secret_name_label_y(a.y), lab, colour, FONT_SMALL);
    }
  }
}

// The secret volume's tic-clock scan, beside trigger_tic (chains answer to "Secret triggers", the volume to "Secret
// areas" plus its sub-switch). A sector pass that never touches the per-walk sprite cycle counter.
static bool secvol_diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, TRIG_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

// One room's wall loop, staged into s_secWalls, or a curtain if it doesn't fit or can't be read. A refused read costs
// the whole room: only reads that CAN report failure (point2 as an index, the slope functions) are trusted, and failure
// rolls back to the curtain; half a room drawn lies about where the region ends.
static int secvol_stage_room(int sect, int wallptr, int wallnum, int nw,
                             int cx, int cy, const short* sintable, bool* fallback) {
  const int first = s_secWallCount;
  bool fb = false;
  const int plan = trigvol_stage_plan(wallnum, TRIGVOL_WALLS_PER_SECTOR,
                                      s_secWallCount, TRIGVOL_WALLS_MAX, &fb);
  bool staged = false;
  if (plan > 0) {
    staged = wallptr >= 0 && wallptr <= nw - plan;
    for (int i = 0; staged && i < plan; i++) {
      const int w  = wallptr + i;
      const int p2 = engine_wall_field_s(w, rva::WAL_POINT2);
      if (p2 < 0 || p2 >= nw) { staged = false; break; }
      TrigWall& tw = s_secWalls[s_secWallCount];
      tw.sector = sect;
      tw.x0 = engine_wall_field_l(w,  WAL_X); tw.y0 = engine_wall_field_l(w,  WAL_Y);
      tw.x1 = engine_wall_field_l(p2, WAL_X); tw.y1 = engine_wall_field_l(p2, WAL_Y);
      if (!engine_ceilz(sect, tw.x0, tw.y0, &tw.z0top) ||
          !engine_florz(sect, tw.x0, tw.y0, &tw.z0bot) ||
          !engine_ceilz(sect, tw.x1, tw.y1, &tw.z1top) ||
          !engine_florz(sect, tw.x1, tw.y1, &tw.z1bot)) { staged = false; break; }
      s_secWallCount++;
    }
    if (!staged) { s_secWallCount = first; fb = true; }
  }
  if (fallback) *fallback = fb;
  if (staged) return plan;

  // The curtain: one panel at the room's centre along its first wall (the one orientation every sector has). A room that
  // can't be measured draws nothing rather than a panel at a guessed height.
  if (s_secWallCount >= TRIGVOL_WALLS_MAX) return 0;
  int ztop = 0, zbot = 0;
  if (!engine_ceilz(sect, cx, cy, &ztop) || !engine_florz(sect, cx, cy, &zbot)) return 0;
  int ang = 0;
  if (wallptr >= 0 && wallptr < nw) {
    const int p2 = engine_wall_field_s(wallptr, rva::WAL_POINT2);
    if (p2 >= 0 && p2 < nw)
      ang = secretvol_curtain_angle(
          engine_getangle(engine_wall_field_l(p2, WAL_X) - engine_wall_field_l(wallptr, WAL_X),
                          engine_wall_field_l(p2, WAL_Y) - engine_wall_field_l(wallptr, WAL_Y)));
  }
  trigvol_curtain(cx, cy, ang & 2047, TRIGVOL_CURTAIN_HALF, ztop, zbot,
                  sintable, &s_secWalls[s_secWallCount]);
  s_secWallCount++;
  return 1;
}

void secret_volume_tic() {
  // Off means no scan and no stale rooms. Both switches: "Secret areas" is the parent, the sub-row the feature.
  const bool on = g_cfg.espsecrets != 0 && g_cfg.secretvolume != 0;
  if (!on || !engine_verified()) {
    s_secCount = 0;
    s_secWallCount = 0;
    s_secScanned = false;
    s_secOn = on;
    return;
  }

  const int nsectors = engine_numsectors();
  const int nwalls   = engine_numwalls();
  const unsigned sig = trigger_map_signature(nsectors, nwalls, 0);
  const unsigned now = wallclock_ms();
  // Signature first, timer second (found secrets go at most a second stale); the switch joins the signature test so
  // flipping it shows rooms at once.
  if (s_secScanned && sig == s_secSig && on == s_secOn &&
      (now - s_secLastScanMs) < TRIG_SCAN_PERIOD_MS) return;

  const int nsec = (nsectors < (int)rva::MAX_SECTORS) ? nsectors : (int)rva::MAX_SECTORS;
  const int nw   = (nwalls   < (int)rva::MAX_WALLS)   ? nwalls   : (int)rva::MAX_WALLS;
  const short* sintable = (const short*)(g_base + rva::SINTABLE);

  s_secWallCount = 0;
  int count = 0, fallbacks = 0, nocentre = 0;
  bool overflow = false;
  for (int sct = 0; sct < nsec; sct++) {
    // The one test deciding what draws (trigger.h): a refused read (-1) is never the secret lotag.
    if (!secretvol_wanted(engine_sector_lotag(sct))) continue;
    if (count >= SECVOL_MAX_SECTORS) { overflow = true; break; }
    // Room centre: the secret marker's wall-vertex average, so the panels and the cross agree.
    int cx = 0, cy = 0, cz = 0;
    if (!engine_sector_centre(sct, &cx, &cy, &cz)) { nocentre++; continue; }
    bool fb = false;
    const int n = secvol_stage_room(sct, engine_sector_field_s(sct, SEC_WALLPTR),
                                    engine_sector_field_s(sct, SEC_WALLNUM),
                                    nw, cx, cy, sintable, &fb);
    if (fb) fallbacks++;
    if (n <= 0) continue;
    s_secFirst[count] = s_secWallCount - n;
    s_secN[count]     = n;
    s_secSect[count]  = sct;
    s_secCx[count]    = cx;
    s_secCy[count]    = cy;
    count++;
  }

  s_secSig = sig;
  s_secLastScanMs = now;
  s_secScanned = true;
  s_secOn = on;
  s_secCount = count;

  // s_secSect names WHICH rooms, for checking against the level's secrets; built inside the throttle.
  if (g_cfg.debuglog != 0 && secvol_diag_due()) {
    char sects[SECVOL_MAX_SECTORS * 8];
    int at = 0;
    sects[0] = 0;
    for (int r = 0; r < s_secCount && at < (int)sizeof(sects) - 1; r++) {
      const int wrote = _snprintf_s(sects + at, sizeof(sects) - at, _TRUNCATE,
                                    r ? ",%d" : "%d", s_secSect[r]);
      if (wrote < 0) break;
      at += wrote;
    }
    LOGI("secretvol: sectors=%d walls=%d fallback=%d nocentre=%d overflow=%d sects=%s",
         s_secCount, s_secWallCount, fallbacks, nocentre, overflow ? 1 : 0, sects);
  }
}

// The in-world producer must run where world_draw_frame calls it: the queue resets at the top, so a collector elsewhere
// has its quads dropped. Worst case: 512 walls x four projections per frame after range cull; WORLD_QUAD_MAX overflows first.
void secret_volume_collect() {
  // The sub-switch's gate, parent first; world_frame_valid means this frame has a capture to draw with.
  if (!g_cfg.espsecrets || !g_cfg.secretvolume) return;
  if (!engine_verified() || !world_frame_valid()) return;
  if (s_secCount <= 0 || s_secWallCount <= 0) return;

  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);

  for (int r = 0; r < s_secCount && r < SECVOL_MAX_SECTORS; r++) {
    const int n = s_secN[r], first = s_secFirst[r];
    if (n <= 0 || first < 0 || first > s_secWallCount - n) continue;
    // Still a secret, asked THIS frame: the engine clears a sector's lotag the instant the player stands in it, so
    // without this the panels would linger up to a second after the cross vanished. -1 reads as "not a secret".
    if (!secretvol_wanted(engine_sector_lotag(s_secSect[r]))) continue;
    // The secret marker's draw-distance rule on the same centre, in the world (a projected point zeroes on refusal).
    if (!trigger_end_in_range(trigger_world_dist2d(cam.posx, cam.posy, s_secCx[r], s_secCy[r]),
                              g_cfg.espdrawdist)) continue;

    for (int i = 0; i < n; i++) {
      const TrigWall& w = s_secWalls[first + i];
      WorldXY a, b;
      int aTop = 0, aBot = 0, bTop = 0, bBot = 0;
      if (!trigvol_quad_corners(w, &a, &b, &aTop, &aBot, &bTop, &bBot)) continue;
      // All four corners or nothing: a corner behind the camera skips the panel. Wound as the tint's wall-switch face.
      WorldQuad q;
      if (!world_view_point(cam, g, sintable, a.x, a.y, aTop, &q.p[0])) continue;
      if (!world_view_point(cam, g, sintable, b.x, b.y, bTop, &q.p[1])) continue;
      if (!world_view_point(cam, g, sintable, b.x, b.y, bBot, &q.p[2])) continue;
      if (!world_view_point(cam, g, sintable, a.x, a.y, aBot, &q.p[3])) continue;
      q.rgba = C_SECRET_VOLUME;
      world_push_quad(q);
    }
  }
}
