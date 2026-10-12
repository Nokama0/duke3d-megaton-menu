# Aim-ahead: why the aimbot missed while the player was moving

Read from `duke3d.exe` in Ghidra (static); the measured step sizes come from the `aim shot:`
log instrument in game. The aimbot's solve is built one tic behind the shot on both ends, and
the menu advances both ends by one tic before solving.

## The order inside `processinput`

`processinput` body VA 0x004fa450-0x004fed2f. Three things happen in this order, and the
order is the whole defect:

1. **The old position is saved.** `oposx/oposy/oposz` are written at
   VA 0x004fb020-0x004fb03e, i.e. before the player has moved this tic. They are
   `ps[] + 568/572/576` (0x238/0x23C/0x240), the same three longs the view freeze
   writes (`rva::PS_OPOSX/Y/Z`, common.h).
2. **The player is moved.** `clipmove(&posx, &posy, &posz, &cursectnum, posxv, posyv,
   164, 0x400, ..)` at VA 0x004fcf4d-0x004fcf9f. After this call `posx/posy/posz` hold
   this tic's position and `oposx/oposy/oposz` hold the previous tic's, so
   `pos - opos` is exactly one tic of the player's own movement.
3. **The weapon section runs and `shoot` is called**, VA 0x004fe462-0x004fed02: the
   sixteen `PUSH <atwith>; PUSH ps[].i; CALL 0x0040316b` sites listed in
   [targeting.md](targeting.md), section on the player's own `atwith` values.

`shoot` itself builds its origin from the player's CURRENT position:
`sx = ps[p].posx; sy = posy; sz = posz + pyoff + 0x400` at VA 0x004f36c0-0x004f36d3,
and reads `sa = ps[p].ang` at VA 0x004f36e0. So the shot leaves from the position
step 2 has just written.

## Where the aimbot's own solve sits

`aim_tic` and `aim_apply` are called from the processinput hook's TAIL (input.cpp:
`aim_tic(pa); aim_apply(pa);`), i.e. after `s_origProcessInput` has already returned,
after step 3 above has already fired the weapon.

So on tic T:

| | what the shot uses | what the solve was built from |
|---|---|---|
| origin | position after T's clipmove | position at the end of T-1 |
| target | enemy after `moveactors(T-1)` | enemy after `moveactors(T-2)` |

Both ends are one tic stale, for two different reasons: the origin
because the aimbot writes after the move, the target because `moveactors` records
`bposx/y/z` at the top of each actor's think and `aim_tic` samples from processinput,
which runs earlier in the same `domovethings`.

## What that costs

Measured with the `aim shot:` instrument's own `step=` field: twenty logged shots reached
(-163, -79, 0), (0, 163, 0) and (12, -242, 0). A run is about 160 xy units on one axis alone and
up to 242 with both at once per tic. At a few hundred units of range a lateral offset
of tens of units is degrees of yaw, and a Build unit of yaw is 360/2048 degrees, so a
strafing player's shot lands an enemy's width off, and a standing player's lands
exactly.

Silent aim is affected less, not differently: `silent.cpp`'s `hooked_shoot` re-solves
from the live `PlayerAim` at the instant of the shot (`silent_resolve`, target.h), so
its origin is already the shot's own. Its target end is still the acquisition's.

## The fix, and why it is not scaled

Both ends advance by one tic before the solve: the origin gains `pos - opos`, the aim
point gains one tic of the smoothed velocity `aim_vel_update` already produces.

**The advance is not scaled by `aimleadscale`.** That dial exists because the
flight-time lead GUESSES what a burst-and-stop Duke actor will do over a rocket's
flight, and a guess deserves to be dialled back. The one-tic advance is not a guess: it
is a latency the engine's own order inside `processinput` guarantees, on both ends,
every tic the player is moving.

**A step larger than 1024 units is dropped** (`AIM_AHEAD_MAX_STEP`, target.h). A run of up to
about 242 units is walking; a step of thousands or millions is a
teleport (`movetransports`), a level change, or a bad read, and advancing an aim point
through one would invent an origin nowhere near where the shot leaves. 1024 is one
full grid square a tic: no legitimate movement reaches it (a cap of 256 was only 1.5x the
measured strafe, and a diagonal run at ~230 sat inside it by chance), and it still leaves a
teleport or a level change thousands of units clear of it.

The vertical cap is separate (`AIM_AHEAD_MAX_STEP_Z`, target.h, the same number as
`AIM_VEL_MAX_FALL`): Build z is 16x finer than xy, so the xy cap would drop a jumping
or falling player's own vertical advance on exactly the tics it is largest. Measured vertical
`step=` readings stay inside it: -1468 (a jump), -998 (a fall) and +1024 (stairs), against the
6144-unit z cap.

## How to read it in the log

With `debuglog` on:

- `aim diag: ... ahead=(dx,dy,dz) noaheadang=A ... ang=B`, throttled to one line every
  three seconds. `ahead` is the three steps this tic (zero standing still, zero for a
  refused warp); `noaheadang` (A) is the yaw the UNADVANCED solve would have produced
  from the same led point, and `ang - noaheadang` (B - A) is the correction in Build
  units.
- `aim shot: #n target=T atwith=0xW ang=P exact=E angerr=D horiz=H step=(x,y,z)`, the
  first twenty player shots that silent aim did not substitute. `exact` is
  `engine_getangle` from the LIVE origin to the published aim point at the instant
  `shoot` runs, and `angerr` is the wrapped signed miss. With the fix it reads near
  zero while strafing; the residual is the target's own motion since the snapshot plus
  whatever `aimsmooth` is set to.
