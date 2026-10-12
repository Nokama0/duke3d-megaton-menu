#pragma once
#include "common.h"
#include "target.h"   // aim_within_range, reused

// The triggerbot. This header decides everything; tbot.cpp gathers the engine's answers and ORs the fire BIT (not a call to
// shoot: weapon cycle, ammo and fire rate hang off `sync.bits & 4`) into the tic's input. Aimbot mode also casts a ray, or
// silent aim would fire into a force field (cansee sees through it).

const int TBOT_WHEN_CROSSHAIR = 0;   // the crosshair's own hitscan lands on a live enemy
const int TBOT_WHEN_AIMBOT    = 1;   // the aimbot's published target, with line of sight
const int TBOT_DELAY_MS_MIN = 0, TBOT_DELAY_MS_MAX = 500;
// No delay fires as soon as the condition holds; the Delay row's reset glyph must match config.cpp.
const int TBOT_DELAY_MS_DEFAULT = TBOT_DELAY_MS_MIN;
const int TBOT_TICS_PER_SECOND = 30;
const int TBOT_HELD_TICS_CAP = 1000000;

struct TbotState { int heldTics; bool firing; };
// One condition's answer and, when it fails, why (the diagnostic prints it).
struct TbotCond  { bool holds; const char* reason; };

inline int tbot_when_clamped(int when) { return when == TBOT_WHEN_AIMBOT ? TBOT_WHEN_AIMBOT : TBOT_WHEN_CROSSHAIR; }
inline int tbot_delay_ms_clamped(int ms) {
  return ms < TBOT_DELAY_MS_MIN ? TBOT_DELAY_MS_MIN : (ms > TBOT_DELAY_MS_MAX ? TBOT_DELAY_MS_MAX : ms);
}
// Milliseconds to the nearest whole 33 ms tic.
inline int tbot_delay_tics(int ms) {
  const int c = tbot_delay_ms_clamped(ms);
  return (c * TBOT_TICS_PER_SECOND + 500) / 1000;
}

// Crosshair mode: the ray hit a live badguy inside the aimbot's own range (aimrange).
inline TbotCond tbot_when_crosshair(int hitsprite, bool badguy, bool alive, int dist2d, int range) {
  TbotCond c = { false, "nohit" };
  if (hitsprite < 0) return c;
  if (!badguy || !alive) { c.reason = "notenemy"; return c; }
  if (!aim_within_range(dist2d, range)) { c.reason = "range"; return c; }
  c.holds = true; c.reason = "hit";
  return c;
}

// Aimbot mode: a published target with line of sight AND a shot ray landing on a live enemy now (the snapshot is last tic's,
// so a lock the current ray misses must not fire). `rayHitsBreakable` (glass, vent grate) only widens "blocked" into a hold.
inline TbotCond tbot_when_aimbot(int targetIndex, bool los, bool rayHitsEnemy, bool rayHitsBreakable) {
  TbotCond c = { false, "nolock" };
  if (targetIndex < 0 || !los) return c;
  if (rayHitsEnemy) { c.holds = true; c.reason = "lock"; return c; }
  if (rayHitsBreakable) { c.holds = true; c.reason = "breaking"; return c; }
  c.reason = "blocked";
  return c;
}

// What the engine itself breaks: checkhitwall (VA 0x0050fcd0) and checkhitsprite (VA 0x00510ec0), rules from
// docs/re/breakable-obstacles.md.

// checkhitwall's gate: hittable (cstat 0x10, or the force-field tile 0xe6, which opens nothing), a neighbour, the hit below
// its ceiling gap, and overpicnum 0x1f7 (glass), 0x197 (fan) or 0x1fe (coloured glass).
inline bool tbot_wall_breakable(int wallCstat, int overpicnum, int nextsector, int hitz, int nextFloorz, int nextCeilingz) {
  const bool hittable = (wallCstat & 0x10) != 0 || overpicnum == 0xe6;
  if (!hittable) return false;
  if (nextsector < 0) return false;
  if (nextFloorz == nextCeilingz) return false;
  if (!(hitz < nextFloorz)) return false;
  return overpicnum == 0x1f7 || overpicnum == 0x197 || overpicnum == 0x1fe;
}

// checkhitsprite's membership list, ascending: picnums a PLAYER hitscan can delete or clear of blocking/hittable bits.
// Runtime gates (damage, hp, a random roll) do not exclude; a gate no hitscan can satisfy does. Per-entry decompile lines:
// docs/re/breakable-obstacles.md.
inline bool tbot_sprite_breakable(int picnum) {
  static const int kBreakable[] = {
    0x156, 0x157, 0x197,
    0x204, 0x205,   // gated on damage
    0x227, 0x22c, 0x22d, 0x237, 0x239, 0x23b, 0x253,
    0x25a, 0x25f,   // gated on damage
    0x29d, 0x2a6, 0x2a8, 0x2ad, 0x2ae, 0x2b1, 0x2b6, 0x2b7, 0x2b9,
    0x2cc, 0x2f1, 0x2fd, 0x300, 0x301, 0x306, 0x30b, 0x318,
    0x365,
    0x385, 0x386,   // gated on the attacker and a 1-in-4 roll
    0x3ba, 0x3bb, 0x3bc, 0x3bd,
    0x3cb, 0x3cc, 0x3cd, 0x3ce, 0x3cf, 0x3d3,
    0x3eb,          // gated on cstat & 1
    0x3f4, 0x3f5, 0x3f6, 0x401, 0x459,
    0x485, 0x486, 0x487, 0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d, 0x48e,
    0x548, 0x549, 0x54a, 0x54b,   // gated on hp
    0x54f,
    0xa1e,          // not a delete: sets xrepeat = 0, which removes it from hitscan's half-width test
    0x1003, 0x115c, 0x1171,
    0x134f,         // gated on hp
  };
  for (int i = 0; i < (int)(sizeof(kBreakable) / sizeof(kBreakable[0])); i++)
    if (kBreakable[i] == picnum) return true;
  return false;
}

// shoot's own ray for the player, no autoaim or spread (VA 0x004f4417-0x004f4476): vx = sintable[(ang + 512) & 2047], vy =
// sintable[ang & 2047], vz = ((100 - horiz - horizoff) << 5) << 6, widened first. Multiplied, not shifted (a negative shift
// is undefined before C++20).
inline void tbot_ray(int ang, int horiz, int horizoff, const short* sintable, int* vx, int* vy, int* vz) {
  if (!sintable || !vx || !vy || !vz) return;
  *vx = sintable[(ang + 512) & 2047];
  *vy = sintable[ang & 2047];
  const long long zvel = ((long long)100 - (long long)horiz - (long long)horizoff) * 32;
  *vz = clamp_ll_to_int(zvel * 64);
}

// The one answer per tic. Every refusal resets the state so a dropped condition restarts its delay. `reason` is the first
// refusal that applied, "delay" while waiting, "fire" when firing.
inline bool tbot_decide(bool on, bool menuOpen, bool freezeActive, bool dead, bool cameraActive,
                        bool weaponUsable, TbotCond cond, int delayTics, TbotState* st, const char** reason) {
  const char* why = nullptr;
  if (!st) { if (reason) *reason = "nostate"; return false; }
  if (!on)              why = "off";
  else if (menuOpen)    why = "menu";
  else if (freezeActive) why = "freeze";
  else if (dead)        why = "dead";
  else if (cameraActive) why = "camera";
  else if (!weaponUsable) why = "weapon";
  else if (!cond.holds) why = cond.reason ? cond.reason : "cond";
  if (why) {
    st->heldTics = 0; st->firing = false;
    if (reason) *reason = why;
    return false;
  }
  if (st->heldTics < TBOT_HELD_TICS_CAP) st->heldTics++;
  st->firing = st->heldTics > delayTics;
  if (reason) *reason = st->firing ? "fire" : "delay";
  return st->firing;
}

// tbot.cpp (DLL only). Tic clock: gather the engine's answers, ask tbot_decide, OR the fire bit into this tic's input on a
// yes. Called from hooked_getinput after mouseaim_tic_apply, before the lockout's `loc` clear; refuses a non-local `player`.
void tbot_tic(short player);

// Whether the LAST tic's decision was to fire. The shot instrument (aim_shot_diag, aim.h) gates on the aimbot's last view
// write, false for a crosshair-mode shot, so it takes either gate. LAST tic's because the shot fires inside processinput,
// after hooked_getinput where tbot_tic runs.
bool tbot_fired_last_tic();
