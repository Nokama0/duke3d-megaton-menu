# Targeting core, aimbot and silent aim: engine facts

What the menu's aim code needs from `duke3d.exe`: line of sight, the engine's own auto-aim,
`shoot`'s angle and pitch maths, the `atwith` values the player passes, projectile speeds,
liveness and velocity of targets, and the clamps the aim write lands on.
Method: Ghidra on the shipping `duke3d.exe` with its PDB. VA = RVA + 0x400000. Every fact names
the instruction it was read from.

Sprite, sector and player layouts and the projection are in esp-sprites.md. The rule that binds
all aim code: **read Polymost's per-frame constants through `engine_gproj()`; never read
`g_cfg.fov` or `viewingrange`.**

## Two rules that bind the design

1. **The two-clock rule.** Rendering runs per frame; input and player think run on the tic
   clock inside `faketimerhandler`. Writing `ps[].ang` / `horiz` from the render clock (the swap
   hook) loses races with the engine's own interpolation (`oang`/`ang`, `ohoriz`/`horiz`,
   `angvel`, `return_to_center`). Aim writes go on the tic clock, in the `processinput` thunk
   hook the DLL owns (`input.cpp`), after the engine has applied the player's own input for
   that tic.

   There are TWO WRITERS and they want opposite things:

   - **A HELD view (the panel's view freeze) COLLAPSES all three interpolated pairs** --
     `oang = ang`, `ohoriz = horiz`, `ohorizoff = horizoff` -- because it pins ONE point
     for as long as the panel is open, and without the collapse Build interpolates between
     two different points on every drawn frame, forever.
   - **A TRACKED view (the aim assist) writes `ang` and `horiz` ONLY**, zeroes `angvel` and
     `return_to_center`, and LEAVES the `o` pairs alone. It rewrites the view every tic, so
     its pair is never two different held points: it is always "last tic's step -> this
     tic's step", which is exactly what the interpolation exists for. The engine copies
     `oang <- ang` / `ohoriz <- horiz` itself at the top of `processinput`
     (**VA 0x004fb057** on the dead-player arm, **VA 0x004fb633** on the main path, and
     **VA 0x004fa648**), which runs BEFORE the DLL's write, so leaving the `o` fields alone
     leaves them holding last tic's value and the renderer draws the frames in between.

   Collapsing a tracked write instead pins every drawn frame to the latest tic and a
   solution that legitimately moves a few units per tic arrives as one visible STEP per
   tic. The write log ruled out a fight with the engine: ten consecutive writes each applied
   exactly, the solved `horiz` walking 72, 83, 89, 94, 100, 104, 110, 115, 118 as the player
   moved.

   The split is `engine_write_player_view` (freeze, collapses) versus
   `engine_write_player_aim` (assist, does not), both driven by the pure, field-by-field
   `view_write_source` in `common.h`.
2. **Read engine state, never re-derive it.** Screen-space aim uses `engine_gproj()`.
   World-space aim uses the live `ps[]` position and angle and the live sprite table.
   Velocities come from the engine's own previous-tic positions (E below).

## A. Line of sight: `cansee`

```
long __cdecl cansee(long x1, long y1, long z1, short sect1,
                    long x2, long y2, long z2, short sect2)   // body VA 0x0043eea0, thunk 0x00403a17
```
Returns non-zero when (x2,y2,z2) is visible from (x1,y1,z1). Pure engine geometry over
`sector[]`/`wall[]`/`sprite[]`; touches no tic-clock state, so it is safe to call from our
`processinput` hook (which already runs on the tic clock) and from the render clock for ESP.

**Eye heights the engine itself uses** (from `aim`, VA 0x004f3010, decompiled):
- Shooter eye: `sprite[player].z - 0x2000` (the player SPRITE, whose z is the feet) or,
  equivalently for the local player, `ps[].posz` directly (the eye).
- Target point for LOS: `sprite[t].z - 0x2000` for everything except `ROTATEGUN` (0x938) and
  0x974 which use `z` unmodified. Use the same.

## B. Duke's own auto-aim: `aim`

```
short __cdecl aim(spritetype* s, short angleRange)            // VA 0x004f3010, thunk 0x00404264
```
Called from `shoot` as `aim(s, 48)` (0x30) at VA 0x004f4117 (hitscan), 0x004f4d7c (laser/spit),
0x004f5125 (RPG family) and two more sites. The cone argument is `PUSH 0x30` at VA 0x004f511f
(`CALL 0x00404264`); the result is stored at `[EBP-0x60]` and `JL 0x004f5205` takes the
no-target branch when it is negative. Returns the sprite index to aim at, or -1.

- **Gate:** `if (s->picnum == APLAYER (0x57d) && ps[s->yvel].auto_aim == 0) return -1;`
  `auto_aim` is `player_struct` +644 (0x284), a `long`. This is the engine's own off switch;
  with it at 0 `aim()` returns -1 at once and walks nothing.
- Cone: a 48-unit half-angle about `s->ang`. Candidates are kept by **smallest projected
  distance along the view axis** -- `local_8 = mulscale(cos(ang), dx, 14) + mulscale(sin(ang),
  dy, 14)`, seeded from `local_40 = 0x7fffffff`, kept when `0x200 < local_8 && local_8 <
  local_40`. That is NEAREST, not nearest-the-crosshair, so with two enemies in the cone it
  need not be the one the assist locked.
- Vertical gate for players: `klabs(scale(dz, 10, dist) - (horiz - 100 + horizoff)) < 100`.
- **Target validity (the engine's own rule):** walks status lists 10, 13, 1, 2;
  `sprite.xrepeat != 0 && sprite.extra >= 0 && (cstat & 0x8101) == 0x101 && (badguy() || list < 2)`,
  excluding APLAYER unless coop, and `pal == 1` targets for weapon 9. Then `cansee` as in A.

**Consequence for silent aim and the aimbot.** When `aim()` returns a sprite, `shoot`
**discards the player's angle and pitch and recomputes both toward that sprite**
(VA 0x004f412f-0x004f4214 for hitscan; 0x004f4d94-0x004f4e40 laser; 0x004f513d-0x004f51c0 RPG):
`zvel = ((sprite[j].z - sz - dal) << 8) / ldist(...)` and `sa = getangle(dx, dy)`.
So a substituted `ps.ang`, and any projectile lead, is only honoured when `aim()` finds
nothing. Details of the RPG/freezer arm (atwith 0xa2d):

- `zvel`: VA 0x004f5175-0x004f51bd -- `ESI = sprite[j].z - sz - dal` (with
  `dal = 2*(yrepeat * tilesizy[picnum]) + 0x800`, VA 0x004f5144-0x004f516b),
  `IMUL ESI, vel` at 0x004f5181, then `IDIV` by `ldist(&sprite[shooter], &sprite[j])`
  (`CALL 0x00401190` at 0x004f51ae), stored at 0x004f51bd.
- `sa` (yaw): VA 0x004f51d5-0x004f51ff -- `getangle(sprite[j].x - sx, sprite[j].y - sy)`
  (`CALL 0x0040434f`), stored at 0x004f51ff. Skipped only when `sprite[j].picnum == 0x7a8`
  (`CMP EAX, 0x7a8; JZ` at 0x004f51ce).
- The no-target branch is VA 0x004f5205-0x004f5230: `zvel = (100 - horiz - horizoff) * 0x51`,
  the path the aimbot's own pitch feeds.

Silent aim therefore sets `ps[].auto_aim = 0` for the duration of the `shoot` call (save, zero,
substitute angle and horiz, call, restore all three). The aimbot deliberately leaves `auto_aim`
alone, so with the game's auto-aim ON the projectile lead never reaches the shot; the marker
still draws what the DLL solved, so `aimlead` must be judged with the game's auto-aim OFF.
"Horiz is mostly cosmetic because Duke's vertical auto-aim handles pitch" holds only while
`auto_aim != 0` and only inside the 48-unit cone.

With `auto_aim` at 0 every `shoot` handler falls to its no-target arm, which reads the player's
view:

- shared hitscan: yaw stays `ps[].ang`, pitch `= (100 - ps[].horiz - ps[].horizoff) * 0x20`
- `case 0x9fc` shrinker: same pair, `* 0x62`
- `case 0xa2d` rpg/freezer: same pair, `* 0x51`

**One consequence of zeroing it, beyond the retarget it prevents.** In `case 0xa2d` the spawned
projectile takes `sprite[j].yvel = (player shooter && aim() found one) ? that sprite : -1` --
the RPG's homing lock. With `auto_aim` held at 0 the lock is always -1 and **a redirected RPG no
longer homes**; it flies at the lead-solved angle instead. That is the intended trade (homing
would re-target it at whatever `aim()` picked rather than at the solved point), and it applies
to redirected shots only.

`horizoff` is deliberately NOT substituted: `target_solve` already solves
`horiz = 100 - horizoff - <slope>`, so the engine's own `100 - horiz - horizoff` reproduces
exactly the intended slope with the player's real `horizoff` left alone.

Scoping the override to the `shoot` call makes it moot whether the game re-copies `ud.auto_aim`
into `ps[].auto_aim` every tic.

## C. `shoot`

```
void __cdecl shoot(short i, short atwith)                      // body VA 0x004f3640-0x004f6346, thunk 0x0040316b
```
Hookable through its thunk exactly like `getinput`/`processinput`/`setaspect` (`hook_jmp_thunk`).

Head (VA 0x004f3647-0x004f37d1): `s = &sprite[i]`; if `s->picnum == APLAYER (0x57d)`:
`p = s->yvel; sx = ps[p].posx; sy = posy; sz = posz + pyoff + 0x400;`
**`sa = ps[p].ang` read at VA 0x004f36e0 (offset +0x28C)**; `crack_time = 777`.
Otherwise (actors) `sa = s->ang`, position from the sprite.

Vertical direction (`zvel`), read from the firing code:
- Hitscan, no auto-aim target (VA 0x004f424f-0x004f4270):
  `zvel = (100 - ps.horiz - ps.horizoff) << 5` (x32). `horiz` +0x220, `horizoff` +0x2BE.
- Hitscan, toward a target (VA 0x004f41ac-0x004f41e7): `zvel = (dz << 8) / dist`.
  Passed to `hitscan(..., zvel << 6, ...)` at VA 0x004f3a1e/0x004f4476.
  => **to hit a point at (dz, dist) with a hitscan: `horiz = 100 - horizoff - (8 * dz) / dist`**
  (dz in z units, dist in xy units; the `aim()` gate's 10 is a loose tolerance, 8 is what fires).
- Laser/spit family, no target (VA 0x004f4e50-0x004f4e6e): `zvel = (100 - horiz - horizoff) * 98`.
- RPG family, no target (VA 0x004f520f-0x004f522d): `zvel = (100 - horiz - horizoff) * 81`.
- Projectiles toward a target (VA 0x004f4dcc-0x004f4e13 / 0x004f5175-0x004f51bd):
  `zvel = ((sprite[j].z - sz - dal) * vel) / ldist(...)` -- the speed IS the scale.
  => **projectile pitch for a point: `zvel = dz * vel / dist`**, and a projectile's motion per
  tic is `(xvel * cos, xvel * sin, zvel)` in engine units (`ssp`), so lead prediction works in
  those units directly.

Spread sites: angle `sa += 16 - (krand() & 31)` at VA 0x004f422c (SHOTSPARK1 with no target)
and 0x004f428c (other hitscan); pitch `zvel += 128 - (krand() & 255)` at 0x004f4273. `krand`
thunk 0x004018d9. (The player-hitscan spread sites are also in triggerbot.md section B.)

## D. Weapons: the `atwith` values, hitscan vs projectile, speeds

### The player's shots

Read from `processinput`'s own call sites. Every row is a literal
`PUSH <atwith>; PUSH ps[].i; CALL 0x0040316b` (the `shoot` thunk); the callers were found with
`get_xrefs_to 0x0040316b`, and these sixteen calls inside `processinput` are every shot the
player can take.

| `atwith` | dec | name | PUSH at | weapon | `shoot` arm |
|---|---|---|---|---|---|
| `0xa23` | 2595 | SHOTSPARK1 | 0x004fe458 | **pistol only** (`kickback_pic == 1`) | shared hitscan |
| `0xa35` | 2613 | **SHOTGUN** | 0x004fe58c ×7 | **shotgun**, 7 pellets at `kb == 4` | shared hitscan |
| `0x9e8` | 2536 | CHAINGUN | 0x004fe876 | chaingun | shared hitscan |
| `0x990` | 2448 | **GROWSPARK** | 0x004fe956 | **expander**, `curr_weapon == 11` | `if (atwith == 0x990)` |
| `0x9fc` | 2556 | SHRINKSPARK | 0x004fe9cf | shrinker, `curr_weapon == 6` | `case 0x9fc` |
| `0xa2d` | 2605 | RPG | 0x004fea65 | rpg | `case 0xa2d` |
| `0xa2d` | 2605 | RPG | 0x004fecf8 | devastator | `case 0xa2d` |
| `0x669` | 1641 | FREEZEBLAST | 0x004feb2b | freezer | `goto`s into `case 0xa2d` |
| `0xa03` | 2563 | TRIPBOMB | 0x004febd8 | tripbomb, wall placement | `case 0xa03` |
| `0x9d9` | 2521 | KNEE | 0x004fec4a | mighty foot, melee | `case 0x9d9` |

`0x9d9` is also pushed by `doincrements` at 0x004f9f8c (`knee_incs == 8`) with the player's own
sprite. Everything else `shoot` dispatches belongs to somebody else: `0x664` SPIT, `0x550`
COOLEXPLOSION1, `0x659` FIRELASER (enemy weapons) and `0x8f8`..`0x8fb` (the blood/decal hitscans
`checkhitsprite` fires back into `shoot` on impact, passing the sprite that was HIT -- which can
be the player's own).

Identities that are easy to get wrong:

1. **`0x9e8` is the chaingun's OWN `atwith`.** Pistol, shotgun and chaingun all spawn
   SHOTSPARK1 (`EGS(..., 0xa23, ...)`) from the shared hitscan body, but that is the SPARK
   SPRITE, not the `atwith` `processinput` passes. Anything filtering on `atwith` must list
   `0x9e8` separately or it silently excludes the chaingun.
2. **The shrinker's `atwith` is `0x9fc`**: the literal `PUSH 0x9fc` at VA 0x004fe9cf, plus
   `shoot`'s `case 0x9fc:` spawning picnum `0x66e` at the fixed 768.
3. **`0x990` is GROWSPARK, the EXPANDER -- not melee.** Three independent reads:
   - the call site is guarded by `MOVSX EDX, word ptr [ECX + 0x2b8]; CMP EDX, 0xb` -- 0x2b8 is
     `curr_weapon`, so this is `curr_weapon == 11` = GROW_WEAPON;
   - it is immediately preceded by `MOV CX, [EAX + 0x2b0]; SUB CX, 1`, and 0x2b0 is
     `ammo_amount[11]` (the array is `short[12]` at 666, so `[6]` is 0x2a6 -- exactly what the
     shrinker's own site decrements, and `shoot`'s tripbomb arm decrements `[8]`);
   - its handler is a full-range `aim()` + `hitscan` + `EGS(..., 0x990, -0x10, 28, 28, ...)` with
     `pal = 2`, `cstat |= 0x82`, `xrepeat = yrepeat = 1` -- vanilla GROWSPARK's invisible marker,
     not a point-blank melee.
4. **`0xa35` is the SHOTGUN, not GROWSPARK.** Seven consecutive `PUSH 0xa35; CALL shoot` at
   VA 0x004fe58c-0x004fe602 (`PUSH 0xa35; PUSH <player sprite i>; CALL 0x0040316b; ADD ESP,8`
   sequences at 0x004fe58c-0x004fe5f8), gated on `kickback_pic == 4` (`CMP <sub-weapon field>,
   4; JNZ` at VA 0x004fe580-0x004fe586) with the shotgun's `kickback_pic == 0x1b` reset just
   above. `shoot` groups it with the chaingun in the "always apply spread" arm. The expander
   is NOT the seven-pellet weapon.

`target_weapon_info` classes the shotgun (2) and expander (11) both `WC_HITSCAN`; silent aim's
redirect set includes `0x990`, so the Expander is redirected.

### The `shoot` dispatch

Dispatch on `atwith` at VA 0x004f37d4-0x004f3886:
- `0x664` SPIT, `0x550` COOLEXPLOSION1, `0x659` FIRELASER -> case VA 0x004f4cef:
  **vel = 840** (VA 0x004f4d58), except COOLEXPLOSION1: 644 if shooter is BOSS2 else 348
  (`vel = 644` at VA 0x004f4d3a). These are enemy weapons; the player never leads with them.
- `0x669` FREEZEBLAST -> case VA 0x004f50df: **vel = 644** (VA 0x004f5105). A literal `goto`
  into the SAME code as the RPG case (`switchD_004f389a_caseD_a2d`):
  `if (atwith != 0x669) return; ... goto caseD_a2d;`.
- `atwith > 0x990` -> jump table on `atwith - 0x9d9` (2521..2613). A 93-byte case-index table at
  VA 0x004f6360 (one byte per `atwith - 0x9d9`, values 0..5) and a 6-entry pointer table at
  VA 0x004f6348 (case number -> handler address). The non-default indices:
  - index 0 (atwith 0x9d9 = 2521): case 0, handler VA 0x004f38f1.
  - indices 15, 74, 92 (atwith 0x9e8 = CHAINGUN, 0xa23 = SHOTSPARK1, 0xa35 = SHOTGUN):
    case 1, handler VA 0x004f40f3 -- the shared hitscan body. No lead, no projectile-velocity
    row, ever.
  - index 35 (atwith 0x9fc = 2556): case 2, the SHRINKER. Spawns picnum 0x66e with a FIXED
    `xvel`-ish param of 0x300 (768) and a horiz/aim-derived pitch -- **no use of 644 anywhere in
    this handler.**
  - index 42 (atwith 0xa03 = 2563): case 3, handler VA 0x004f5940 -- **TRIPBOMB placement**,
    not a pipebomb detonator (spawns picnum 0xa06 aligned to the hit wall via `getangle` on the
    wall's own two vertices, `cstat = 0x10`, `xvel = -0x14`, and decrements
    `ps[].ammo_amount[8]`, the tripbomb ammo slot).
  - index 84 (atwith 0xa2d = 2605, RPG): case 4, handler VA 0x004f50ea =
    `switchD_004f389a_caseD_a2d`, the SAME code FREEZEBLAST reaches via the goto. This is the
    ONLY handler that sets vel = 644: `local_54 = 0x284` at VA 0x004f5105, overridden to 0x304
    (772) **only inside the `local_c < 0` (no player shooter) branch, when the SHOOTER's own
    picnum is 0xa96 -- an enemy-only override that the player's own shots never reach.**
  - every other index: case 5, handler VA 0x004f6342, an immediate `return` (no-op/default).
- `0x3ac`, `0x672` -> VA 0x004f5c00 (tripbomb/laser placement).
- `0x990` GROWSPARK (the expander) has its own full-range `aim()` + `hitscan` arm, see above.

`search_instructions` for the literal `0x284` inside `shoot()` returns exactly four hits:
VA 0x004f4d3a and VA 0x004f5105 are the two `vel = 644` assignments above. VA 0x004f5808 and
VA 0x004f5844 decompile as `MOV ESI, 0x284` immediately followed by `IDIV ESI` -- 644 used as a
DIVISOR, not a speed. They sit inside case 0xa2d's tail, in the `ps[local_c].curr_weapon == 7`
branch (gated further by `hbomb_hold_delay`): `sprite[j].x -= sintable[ang] / 644` and the
same for `.y`. Slot 7 is the devastator (it fires RPG-family projectiles and staggers a SECOND
shot's muzzle position off the first): this is its twin-cannon muzzle offset, a coincidental
reuse of the literal 644, not shrinker/expander code.

**`ssp`'s per-tic displacement.** Thunk VA 0x004040de -> real body VA 0x00498b80, decompiled in
full:
```c
short __cdecl ssp(short i, ulong cliptype) {
  return movesprite(i,
    (sprite[i].xvel * sintable[(sprite[i].ang + 0x200) & 0x7ff]) >> 14,   // dx = xvel*cos(ang)
    (sprite[i].xvel * sintable[ sprite[i].ang        & 0x7ff]) >> 14,    // dy = xvel*sin(ang)
    sprite[i].zvel,                                                      // dz, UNSCALED
    cliptype) == 0;
}
```
For a projectile with `xvel` and `zvel` set once at spawn (the weapon-table `vel` is exactly
this `xvel`), the step `movesprite` is asked to apply is `(xvel*cos(ang), xvel*sin(ang), zvel)`,
with sintable's native 16384 scale (`>>14`) on the xy terms only -- **zvel is added with no
extra scaling**. `movesprite` (not decompiled) still does its own wall/floor/ceiling clipping on
top, so this is the ATTEMPTED step, not a guarantee the sprite moves this far every tic.

So for the player: **hitscan = pistol, shotgun, chaingun, expander (no lead). Projectile at
vel = 644 = RPG, devastator, freezer (the 772 override is enemy-shooter-only). Shrinker is its
own case with a fixed vel = 768. Tripbomb is placement, not a targeting weapon.** Speeds come
from constants in `shoot`, not from the projectile tile. Speed units: engine xy units per tic
along `(cos, sin)`, per `ssp`. Tic rate is the game's 30 Hz `totalclock` (120 ticks/s, 4 per
tic).

## E. Target validity, dying state, and velocity

- The engine's own rule (from `aim`): `xrepeat != 0 && extra >= 0 && (cstat & 0x8101) == 0x101`
  plus `badguy()`.
- **Non-positive `extra` is not death in this game.** The `lost enemy:` instrument logged
  Assault Troopers (1680), Pig Cops (2000, pal 0) and RECON drones (1960) at `extra` 0, -1, -2,
  -7, -9 and lower with **`cstat = 0x0101` still set, `statnum = 1`, `badguy = 1`, `xrepeat`
  non-zero** -- actors the player could still shoot. A Duke death state is what CLEARS the
  blocking/hittable bits (hence walk-through corpses); a genuinely dying octabrain in the same
  log read `cstat = 0x0000`. This is not type- or palette-specific.
  The liveness rule is therefore the cstat rule (`xrepeat != 0 && (cstat & 0x8101) == 0x101`,
  on status lists 1/2, plus `badguy()`), `target_alive_rule`/`target_hittable_rule` in
  `target.h`; `extra` is a number to display and a preference tier for target choice. The
  per-sprite enemy state (actorstate.h) adds `extra` and known action records on top; see
  switches-breakables.md section D.
- **`hittype` (the actor side table): VA 0x019f2880, RVA 0x015F2880, `weaponhit[16384]`,
  stride 72.** Fields: `picnum` +2, `ang` +4, `extra` +6, `owner` +8, `movflag` +10,
  `tempang` +12, `actorstayput` +14, `dispicnum` +16, `timetosleep` +18, `floorz` +20,
  `ceilingz` +24, `lastvx` +28, `lastvy` +32, **`bposx` +36, `bposy` +40, `bposz` +44**,
  `temp_data[6]` +48.
- **Velocity for lead prediction = `sprite[i].x - hittype[i].bposx` (and y, z) per tic.**
  `bpos*` is the previous tic's position, written by the engine's own actor mover; reading
  it is "read engine state", not re-derivation. Bounds-check `i < 16384` and the stride.
- **`hittype[].lastvx` / `lastvy` are a POSITION, not a velocity** (static); not used. They
  hold the last-seen target's world x/y. Three sites settle it:
  - **Write, `parse` (the CON VM) VA 0x004e2266-0x004e228b** -- the `ifcansee` handler. It
    loads a `spritetype*` (the player, or holoduke) and copies its first two longs, i.e.
    `s->x` and `s->y`:
    ```
    004e2270: MOV EDX, dword ptr [ECX]            ; s->x
    004e2275: MOV dword ptr [EAX + 0x19f289c], EDX   ; hittype[i].lastvx = s->x
    004e2288: MOV EDX, dword ptr [ECX + 0x4]      ; s->y
    004e228b: MOV dword ptr [EAX + 0x19f28a0], EDX   ; hittype[i].lastvy = s->y
    ```
  - **Read, `alterang` VA 0x004e0d83/0x004e0d9d** -- used as absolute coordinates:
    `getangle(hittype[g_i].lastvx - g_sp->x, hittype[g_i].lastvy - g_sp->y)` (taken when the
    actor's owner is picnum 0x57d, APLAYER). A velocity would never be differenced against the
    actor's own position.
  - **Zeroed at spawn**, `EGS` VA 0x004c1aa2/0x004c1ab3 and `spawn` VA 0x004c1fd7/0x004c1fe8.

  So there is no `engine_sprite_step` accessor; the lead solver reads
  `position - hittype[].bposx/y/z` and the flicker is handled by the pure smoother in target.h.
- **The two spike caps the smoother rejects on** (both read):
  - **Horizontal, 840 xy-units per tic.** `ssp` (VA 0x00498b80, section D) bounds a sprite's
    horizontal step by its own `xvel`, and the largest `xvel` this binary spawns anything with
    is the SPIT/FIRELASER family's **840** at VA 0x004f4d58. An actor is slower still, so a
    horizontal step above 840 did not come from the mover.
  - **Vertical, 6144 z-units per tic.** `makeitfall` clamps a falling sprite's `zvel` itself and
    then adds it straight to `z`:
    ```
    VA 0x004dd179  CMP  EAX, 0x1800        ; zvel vs 6144
                   ... else sprite->zvel = 0x1800
                   sprite->z = sprite->zvel + sprite->z
    ```
    So 6144 is the fastest the engine's own faller moves a sprite in one tic. The two caps are
    deliberately different numbers: reusing 840 for z would reject every falling enemy, and a
    rejection drops the whole sample, including the horizontal velocity the lead needs.

## F. Aim points (the engine's own)

With `h = yrepeat * tilesizy[picnum]` (sprite height is `4h` in z units, esp-sprites.md):
- Hitscan auto-aim point: `z_aim = sprite.z - (2h + 0x500)`, and `-0x800` more for
  ROTATEGUN / 0x942..0x949 (VA 0x004f4156-0x004f4196). That is mid-body plus 1280.
- Laser family: `z - (2h - 0xC00)`; RPG family: `z - (2h + 0x800)` (VA 0x004f4dbb, 0x004f5164).
- "Body centre" is `z - 2h`; "upper body" is `z - 3h` (three quarters up; the head is at
  `z - 4h`). Both are trivial from `proj_sprite_extents`'s inputs.

## G. Angle maths

- `long __cdecl getangle(long dx, long dy)` VA 0x0043e550 (thunk 0x0040434f): Build angle
  0..2047 of the vector, the same convention `sintable` uses. Safe to call.
- `long __cdecl ksqrt(long)` VA 0x0043e670; `ldist`/`dist` thunks 0x00401190/0x00402bad
  (sprite-to-sprite distance helpers used by `shoot`).
- `sintable` RVA 0x01A8B020, 16384-scaled; cos = `sintable[(a + 512) & 2047]`.
- Pitch: see C. `horiz` is a `long` at +0x220, neutral 100; larger = looking up; `horizoff`
  +0x2BE is a short added by the engine (e.g. slopes) and must be included.
- The player's aim direction unit vector is `(cos(ang), sin(ang))` in xy with `zvel` per C.

## H. Hold-key on a mouse side button

`handleevents` (VA 0x00495030) calls `dnUpdateMouseInput` (VA 0x00407e20), which consumes
**only** `MANYMOUSE_EVENT_RELMOTION` into `s_dx`/`s_dy`; ManyMouse carries no buttons in
this build (static). Mouse buttons therefore arrive as SDL `SDL_MOUSEBUTTONDOWN/UP` events
(0x401/0x402), which `input.cpp`'s `hooked_poll` already sees (button byte at event offset 16,
filtered to `== 1`). SDL2 numbering: 1 left, 2 middle, 3 right, **4 = X1, 5 = X2**. So
"Mouse 5" is SDL button 5 in the hook the DLL already owns; no engine global is needed.
(`mouseb` at VA 0x00E2E870 is read by `readmousebstatus` and written only by
`MOUSE_ClearButton`; its raw-input writer is not reachable by operand search and is not needed.)

## I. `processinput`'s `horiz` clamp and the pitch deadzone

`processinput` (VA 0x004FA450, reached through the thunk at `rva::THUNK_PROCESSINPUT_RVA`,
VA 0x00402A63) clamps its own `horiz` in exactly one place (0x220 is `rva::PS_OFFSET_HORIZ`,
the offset the view freeze writes):

```
VA 0x004fd559  CMP  dword ptr [EDX + 0x220], 0x12b          ; horiz vs 299
VA 0x004fd568  MOV  dword ptr [EAX + 0x220], 0x12b          ; horiz = 299
VA 0x004fd577  CMP  dword ptr [ECX + 0x220], -0x63          ; horiz vs -99
VA 0x004fd583  MOV  dword ptr [EDX + 0x220], 0xffffff9d     ; horiz = -99
```

Decompiled: `if (horiz < 300) { if (horiz < -99) horiz = -99; } else horiz = 299;`, i.e. the usable
range is **-99 .. 299 inclusive**, pinned as `AIM_HORIZ_MIN`/`AIM_HORIZ_MAX` in target.h. It
agrees with the stock Duke source. The aimbot's write lands AFTER `s_origProcessInput` has
already clamped, so an unclamped solved pitch would sit in the field for a whole tic.

**The mouse-aim pitch deadzone: 96..104 -> 100.** Immediately above that clamp:

```
VA 0x004fd4da  CMP  dword ptr [EAX + 0x280], 0x0    ; aim_mode (mouse aim) on?
VA 0x004fd4e1  JZ   0x004fd507                      ; off -> the snap below
               ... on: MOVSX EDX, byte [sync + 0x21]; SAR EDX,1
VA 0x004fd4f6  ADD  EDX, dword ptr [EAX + 0x220]    ;   horiz += sync[].horz >> 1
VA 0x004fd50a  CMP  dword ptr [EDX + 0x220], 0x5f   ; off: if (horiz > 95
VA 0x004fd511  JLE  0x004fd52c
VA 0x004fd516  CMP  dword ptr [EAX + 0x220], 0x69   ;         && horiz < 105)
VA 0x004fd51d  JGE  0x004fd52c
VA 0x004fd522  MOV  dword ptr [ECX + 0x220], 0x64   ;   horiz = 100
```

**`ps[] + 0x280` is `aim_mode`**, identified by adjacency rather than a name: it is the long
immediately before `rva::PS_AUTO_AIM` (0x284), the `int32_t aim_mode, auto_aim` pair Duke's
player struct has, and its two branches are exactly free-aim versus snap-to-level.

With mouse aiming **off**, the engine snaps any pitch in **96..104 to exactly 100** at the start
of every tic -- before the shot is fired inside the same `processinput`, and before the aimbot's
write lands after it. The rendered view still shows the aimbot's pitch (the write comes later),
so this is never visible as jitter; it costs a small vertical miss on a near-eye-level target,
for players with mouse aiming disabled only. The assist does not model it. Keep mouse aiming on.

## Player struct offsets used by aim (stride 940, PDB layout)

`posx` 532 (0x214), `posy` 536, `posz` 540 (0x21C, the eye), `horiz` 544 (0x220), `ohoriz` 548,
`ohorizoff` 552, `pyoff` 580, `auto_aim` 644 (0x284), `ang` 652 (0x28C), `oang` 654, `angvel` 656,
`cursectnum` 658 (0x292), `curr_weapon` 696 (0x2B8), `horizoff` 702 (0x2BE), `i` 742 (0x2E6,
the player's sprite index), `dead_flag` 812, `return_to_center` 920 (0x398).

## Open questions

- Whether `ud.auto_aim` is re-copied into `ps[].auto_aim` each tic (moot while the override is
  scoped to the `shoot` call).
