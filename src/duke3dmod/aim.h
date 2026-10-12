#pragma once
#include "engine.h"

// Target acquisition on the tic clock, published to the render clock under a seqlock. aim_apply is the sole consumer
// that writes the view, gated as aim_tic refuses: exactly one place the assist switches off.

struct AimSnapshot {
  int  targetIndex;            // selected enemy's sprite index, or -1
  // The tile at acquisition: a Build sprite slot is recycled the instant its actor dies; silent.cpp's re-solve checks it.
  int  targetPicnum;
  // Which target_probe_z rung (0 configured, 1 upper, 2 head) cansee accepted.
  int  targetRung;
  int  aimx, aimy, aimz;       // the aim point AFTER projectile lead
  int  rawx, rawy, rawz;       // BEFORE lead; the marker draws both
  int  solvedAng, solvedHoriz; // the view angle and pitch that hit the aim point
  int  leadScale;              // g_cfg.aimleadscale as clamped for this tic
  int  choice;                 // AimSolutionChoice: which solve was published
  // Belt to targetIndex: aim_tic publishes only a target that passed cansee.
  bool los;
  unsigned seq;                // seqlock counter: even = stable, odd = being written
};

// One tic's acquisition for the local player; empty when aim_should_run refuses, the weapon has no aim class or nothing
// survives. Call from hooked_processinput after the original, freeze off; never on render.
void aim_tic(const PlayerAim& p);

// Publishes the empty snapshot: the freeze path skips aim_tic, which would leave the last target frozen.
void aim_clear();

// Render-clock read under the seqlock; false (draw nothing) if the copy wasn't clean or `out` is null.
bool aim_snapshot(AimSnapshot* out);

// The average s_vel holds for (index, picnum): last tic's fold, since hooked_shoot runs before aim_tic.
bool aim_smoothed_velocity(int index, int picnum, int* vx, int* vy, int* vz);

bool aim_hold_active();

// Whether the aimbot's view write landed on the LAST tic, for hooked_getinput's hold-view clear (the next mouse turn is
// added at the top of the next call). False on every refused tic.
bool aim_view_held();

void aim_view_hold_note(bool held);   // what aim_hold_view_rule answered, for the `aim diag:` line

// Writes the snapshot into the view, eased by g_cfg.aimsmooth and clamped to the pitch envelope; right after aim_tic only,
// non-collapsing. The shot fires BEFORE this write (~38 ms of two-clock lag). Engine auto_aim is NOT harmless: aim() (VA
// 0x004f3010) re-targets the shot and shoot recomputes zvel and yaw (VA 0x004f5175-0x004f51ff), so Duke's auto-aim must be OFF.
bool aim_apply(const PlayerAim& p);

// Shot-time measurement: the live miss at the instant `shoot` runs (from silent.cpp's hooked_shoot). Logs the first
// AIM_SHOT_DIAG_MAX_LINES shots with debuglog on, gated so acquisition alone does not measure the player's own aim
// error; `via=aim|tbot|silent` says which. `aa=%d` is autoAim: tbot_ray ignores it, but aim() can redirect off the ray.
void aim_shot_diag(const PlayerAim& p, int atwith, bool redirect);

// Zeroes the shot instrument's session budget on a level change (actorstate.cpp).
void aim_shot_budget_reset();

// The last shot line written: number, wallclock ms and target index (zero and -1 until the first), so dmgnum.cpp's hit
// line can say which shot it belongs to.

void aim_last_shot(unsigned* no, unsigned* ms, int* target);
