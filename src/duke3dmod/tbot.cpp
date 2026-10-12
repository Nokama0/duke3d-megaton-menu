#include <windows.h>   // GetTickCount, throttled diagnostic only
#include "tbot.h"
#include "engine.h"
#include "aim.h"
#include "target.h"
#include "actorstate.h"
#include "trigger.h"
#include "input.h"
#include "config.h"
#include "hooks.h"
#include "common.h"
#include "log.h"

// Decides nothing (tbot.h does): gathers engine answers, calls tbot_decide, sets the bit. Runs in hooked_getinput after
// mouseaim_tic_apply, before the lockout's loc clear: processinput has already read sync.bits when its hook writes.

static TbotState s_state = { 0, false };
static bool      s_lastFiring = false;
static unsigned  s_edgeLines = 0;
const unsigned   TBOT_EDGE_LINES_MAX = 50;
const unsigned   TBOT_DIAG_WINDOW_MS = 1000;

static bool tbot_diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, TBOT_DIAG_WINDOW_MS)) return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

struct TbotHit { int sprite, wall, sect, hx, hy, hz; };

// The ray and cast shared by crosshair and aimbot mode: shoot's OWN ray (tbot_ray: no autoaim, no spread) from shoot's
// OWN origin, fail-closed like engine_hitscan. `sprite` is bounds-checked, `wall` is not (live count): callers must.
static TbotHit tbot_cast_at(const PlayerAim& p, int ang, int horiz) {
  TbotHit h = { -1, -1, -1, 0, 0, 0 };
  if (!engine_verified()) return h;
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  int vx = 0, vy = 0, vz = 0;
  tbot_ray(ang, horiz, p.horizoff, sintable, &vx, &vy, &vz);
  const int sz = aim_shot_origin_z(p.eyez, p.pyoff);   // shoot's sz: posz + pyoff + 0x400
  int hitsect = -1, hitwall = -1, hitsprite = -1, hx = 0, hy = 0, hz = 0;
  if (!engine_hitscan(p.eyex, p.eyey, sz, p.cursectnum, vx, vy, vz,
                      &hitsect, &hitwall, &hitsprite, &hx, &hy, &hz))
    return h;
  h.wall = hitwall;
  h.sect = hitsect;
  h.hx = hx; h.hy = hy; h.hz = hz;
  if (hitsprite >= 0 && hitsprite < rva::MAX_SPRITES) h.sprite = hitsprite;
  return h;
}
static TbotHit tbot_cast(const PlayerAim& p) { return tbot_cast_at(p, p.ang, p.horiz); }

// Crosshair mode: the cast, then the aimbot's enemy rules; `badguy` is reported apart to tell "notenemy" from a wall hit.
static TbotCond tbot_crosshair(const PlayerAim& p, int* hitOut, int* distOut) {
  const TbotHit h = tbot_cast(p);
  *hitOut = h.sprite;
  const int hitsprite = *hitOut;
  *distOut = hitsprite < 0 ? 0 : trigger_world_dist2d(p.eyex, p.eyey,
                                  engine_sprite_field_l(hitsprite, SPR_X),
                                  engine_sprite_field_l(hitsprite, SPR_Y));
  if (hitsprite < 0) return tbot_when_crosshair(-1, false, false, 0, g_cfg.aimrange);
  const bool badguy = engine_is_badguy(hitsprite);
  // `alive` is the targetable state set the aimbot walk and silent aim share, so we stop pulling on a dying body. The
  // answer is up to one tic (~38 ms) old, versus ~0.8 s for the old viewtype rule.
  const bool alive = actor_state_targetable(actor_state_cached(hitsprite));
  return tbot_when_crosshair(hitsprite, badguy, alive, *distOut, g_cfg.aimrange);
}

void tbot_tic(short player) {
  const PlayerAim p = engine_player_aim();
  if (p.index < 0 || (int)player != p.index) return;     // the local player only
  const int when = tbot_when_clamped(g_cfg.tbotwhen);
  const WeaponInfo w = target_weapon_info(p.currWeapon);

  // input_lockout_now() is mouseaim.cpp's rule; engine_paused() joins it because hooked_getinput still runs every frame
  // while paused, which would OR the fire bit in once per FRAME rather than per tic.
  const bool freezeActive = input_lockout_now() || engine_paused();
  const bool weaponUsable = p.valid && aim_weapon_usable(w.cls);
  const bool cheapRefusal = !g_cfg.tboton || input_menu_open() || freezeActive
                          || p.dead || p.cameraActive || !weaponUsable;

  int hit = -1, dist = 0, target = -1;
  // The obstacle the ray stopped on, for the `tbot:` line; all stay "nothing to report" outside the aimbot branch.
  int hitwall = -1, wover = 0, wcstat = 0, spic = -1;
  int rayVia = 0;   // 0 the view's angles, 1 silent aim's solved pair
  bool rayHitsBreakable = false;
  TbotCond cond = { false, "skipped" };
  if (!cheapRefusal) {
    if (when == TBOT_WHEN_AIMBOT) {
      AimSnapshot s;
      const bool have = aim_snapshot(&s);
      target = have ? s.targetIndex : -1;
      // The snapshot is LAST tic's publish, so a killed or turned-away target still reads as a lock: cast the same
      // hitscan as crosshair mode (any live enemy in aimrange counts). The ray is the shot's own: the view while
      // steering, else silent aim's solved pair (E1L3 force fields stop a hitscan).
      const bool aimSteering = aim_view_held();
      const bool solvedRay = !aimSteering && g_cfg.silenton != 0 && have;
      rayVia = solvedRay ? 1 : 0;
      bool rayHitsEnemy = false;
      if (target >= 0) {
        const TbotHit h = solvedRay ? tbot_cast_at(p, s.solvedAng, s.solvedHoriz)
                                    : tbot_cast(p);
        hit = h.sprite;
        dist = hit < 0 ? 0 : trigger_world_dist2d(p.eyex, p.eyey,
                             engine_sprite_field_l(hit, SPR_X),
                             engine_sprite_field_l(hit, SPR_Y));
        rayHitsEnemy = hit >= 0 && aim_within_range(dist, g_cfg.aimrange)
                    && actor_state_targetable(actor_state_cached(hit));
        // A ray stopping on a window, vent or breakable prop is not blocked: the engine breaks it on that hit
        // (docs/re/switches-breakables.md). `hitwall` is checked against the LIVE numwalls so a corrupt count cannot
        // widen the trusted window.
        hitwall = h.wall;
        if (hitwall >= 0 && hitwall < engine_numwalls()) {
          wcstat = engine_wall_field_s(hitwall, rva::WAL_CSTAT);
          wover = engine_wall_field_s(hitwall, rva::WAL_OVERPICNUM);
          const int nextsector = engine_wall_field_s(hitwall, rva::WAL_NEXTSECTOR);
          int nextFloorz = 0, nextCeilingz = 0;
          const bool haveSector = nextsector >= 0 && nextsector < engine_numsectors();
          if (haveSector) {
            nextFloorz = engine_sector_field_l(nextsector, SEC_FLOORZ);
            nextCeilingz = engine_sector_field_l(nextsector, SEC_CEILINGZ);
          }
          rayHitsBreakable = tbot_wall_breakable(wcstat, wover, haveSector ? nextsector : -1,
                                                  h.hz, nextFloorz, nextCeilingz);
        }
        if (!rayHitsBreakable && hit >= 0 && !rayHitsEnemy) {
          spic = engine_sprite_field_s(hit, SPR_PICNUM);
          rayHitsBreakable = tbot_sprite_breakable(spic);
        }
      }
      cond = tbot_when_aimbot(target, have && s.los, rayHitsEnemy, rayHitsBreakable);
    } else {
      cond = tbot_crosshair(p, &hit, &dist);
    }
  }
  const char* reason = nullptr;
  const bool fire = tbot_decide(g_cfg.tboton != 0, input_menu_open(), freezeActive,
                                p.dead, p.cameraActive, weaponUsable,
                                cond, tbot_delay_tics(g_cfg.tbotdelay), &s_state, &reason);
  if (fire) engine_input_set_fire();

  // `reason=lock` covers a pull on the locked `target` and on another live enemy the ray landed on.
  if (g_cfg.debuglog != 0) {
    const bool edge = (fire != s_lastFiring) && s_edgeLines < TBOT_EDGE_LINES_MAX;
    if (edge || tbot_diag_due()) {
      if (edge) s_edgeLines++;
      LOGI("tbot: on=%d when=%d firing=%d held=%d delay=%d reason=%s hit=%d dist=%d target=%d "
           "hw=%d wover=%d wcstat=0x%04X spic=%d brk=%d ray=%d",
           g_cfg.tboton, when, fire ? 1 : 0, s_state.heldTics, tbot_delay_tics(g_cfg.tbotdelay),
           reason ? reason : "?", hit, dist, target,
           hitwall, wover, wcstat & 0xFFFF, spic, rayHitsBreakable ? 1 : 0, rayVia);
    }
  }
  s_lastFiring = fire;
}

bool tbot_fired_last_tic() { return s_lastFiring; }
