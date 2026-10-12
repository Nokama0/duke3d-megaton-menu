# Breakable obstacles engine facts: what checkhitwall/checkhitsprite open on a hit

Which walls and sprites a player hitscan opens, so the triggerbot's line-of-sight test does
not treat them as solid. Everything below is a binary read (static) except the last section,
which is the design built on it.
Method: Ghidra decompiles of `checkhitwall` and `checkhitsprite` (dumps `checkhitwall.c`,
`checkhitsprite.c` in the Ghidra project's `dumps` folder; line numbers below are those dumps').

## The problem

A shot at glass breaks it open. The ray test the aimbot-mode condition casts would treat the
surviving frame like any other solid wall, so the very obstacle a real shot sails through would
refuse the trigger pull that fires it. Without the rule, targets behind windows and vents were
never shot at (27 `reason=blocked` tics in one log).

## `checkhitwall`, VA 0x0050fcd0

The function that runs after a wall is hit by a shot. Most of what it does is cosmetic: a tile
swap on a wall that stays exactly as solid as it was (the 0x101b-0x102e and 0x1055 tech walls,
0x2bf/0x2c1, 0x326, 0x363, 0x2bd, 0x7c, 0x78), or a spark that opens nothing at all (overpicnum
0xe6, the force fields, and 0x297-0x299). The 0x230 overlay opens only to an explosive hit, not
to a pistol shot, so a triggerbot shot never opens it either.

A wall becomes PASSABLE -- its `cstat` cleared, or specifically its hittable/blocking bits --
only when every one of these holds:

1. `(wall.cstat & 0x10) != 0` (the wall is marked "hittable" -- masked), OR
   `wall.overpicnum == 0xe6` (230, the force-field spark tile).
2. `wall.nextsector >= 0` (the wall has a real neighbour to open into).
3. The hit z is above the next sector's floor: `hz < sector[nextsector].floorz`, AND that
   sector actually has a gap to fall through: `floorz != ceilingz`.
4. `wall.overpicnum` is one of the three tiles the engine's own switch on it actually opens:
   - `0x1f7` (503): glass. `overpicnum` becomes `504`, `cstat` cleared to 0 on both sides.
   - `0x197` (407): the fan/vent grate. `cstat &= ~0x41` on both sides.
   - `0x1fe` (510): coloured glass. `cstat` cleared to 0 on both sides.

The `overpicnum == 0xe6` branch of condition 1's OR can never itself satisfy condition 4 --
`0xe6` is none of the three listed tiles -- which is exactly why the force fields are "a spark
that opens nothing": the gate lets that hit pass the first test, and the switch on `overpicnum`
still does nothing with it. `tbot_wall_breakable` (tbot.h) reproduces this whole chain verbatim,
condition by condition, rather than folding the dead branch away.

Against `checkhitwall.c` in full: the gate is
lines 30-33 exactly as written above, and the `overpicnum` switch below it (lines 34-112) clears
cstat on precisely three values -- `0x1f7` (`cstat = 0` both sides, :41-:44), `0x197`
(`cstat &= 0xffbe`, clearing 0x41, both sides, :83-:87) and `0x1fe` (`cstat = 0` both sides,
:98-:101). The other two arms of that switch, `0xe6` (:55) and `0x297`-`0x299` (:108), reach
`LAB_0050fe70`, which spawns a spark sprite and touches no `cstat` at all. Everything the
function does below the gate (`breakwall`, the `0x101b`-`0x102e`/`0x1055` tech walls, `0x2bd`,
`0x78`/`0x7a`/`0x7c`, `0x107`-`0x113`, `0x363`) only swaps `picnum`, never `cstat`: the wall
stays exactly as solid as it was. The `0x230` overlay (:17-:29) opens only to the explosive
attacker set `0x4d6/0x1a/0x3d5/0x437/0x4df/0x686/0xa2d`, which does not contain the player
hitscan's SHOTSPARK1 (`0xa23`). The two `cstat`-clearing arms `0x1f7` and `0x1fe` also need
`updatesector` to find a sector at the hit point (:38, :96) -- true wherever a shot actually
lands, so `tbot_wall_breakable` does not model it.

## `checkhitsprite`, VA 0x00510ec0

The function that runs after a sprite is hit by a shot. The table below is derived in one pass
over the WHOLE non-enemy part of the function --
everything before the badguy branch at `switchD_00510f1d_caseD_158` (checkhitsprite.c:201) --
with every `case` label and every `picnum ==` compare traced to its return, and a decompile line
cited per entry. Line numbers are `checkhitsprite.c`'s.

**The membership rule**: a picnum is a member when a hit by the player's hitscan can reach
`deletesprite(param_1)`, or a clear of the sprite's blocking/hittable bits (`cstat = 0`,
`& 0xfefe`, `& 0xfeff`, `& 0xfffe`), before the function returns -- so the next shot passes.
Gates on runtime state (accumulated damage, hit points, a random roll) do not exclude: those
picnums are members, marked below with the gate's line, and **they can refuse an individual
shot** -- see "the conditional members" after the table. A gate a hitscan can never satisfy
does exclude; see "handled but not passable".

**Four ways into a delete that is not on the case label itself** -- each of them a place an
transcription from the switch labels alone loses picnums:

1. A bare `break;` out of an inner switch (`:106` and `:405`) falls past the switch's and its
   enclosing `if`s' closing braces into the shared shard-and-delete block at `:473-:490` -- the
   same block `0x227`/`0x237` reach by an explicit `goto`. There are TWO such groups, one per
   inner switch.
2. `0x365` (`:27`) fails the `if (iVar6 != 0x365)` guard, skips its inner switch entirely, and
   lands on that same block.
3. `0x485`-`0x48e` fall through BOTH enclosing `if`s (`:111` `0x48e < iVar6` and `:314`
   `iVar6 < 0x485`, neither true anywhere in that inclusive range) onto that same block. This is
   how MSVC lays out one contiguous run of case labels sharing one body: narrow to the range,
   then fall straight into it with no further test, so all ten values are members, not just the
   one the shared block happens to name in a bonus-effect `if` (`0x485`, `:474`).
4. A `goto` from a far-away `if` (`:174`, `:179`, `:198`, `:199`) lands on a label inside a
   switch case body it has no `case` of its own in.

That shared block at `:473-:490` runs `spritesound`/`lotsofglass`/`deletesprite` unconditionally
on every path into it; the picnum-specific `if`s at its top (`:474-:484`) only choose a bonus
effect (money, coloured glass, or plain glass shards) first.

| Picnum | Reached via | Effect | Line |
| --- | --- | --- | --- |
| 0x156, 0x157 | `case` :518, :519 | `cstat &= 0xfefe` | :521 |
| 0x197 | `case` :526 | `cstat &= 0xfefe` | :528 |
| 0x204, 0x205 | `case` :558-:559, `goto` caseD_204 | deleted, **gated** :494 | :513 |
| 0x227 | `case` :561, `goto` caseD_227 (shared block) | deleted | :489 |
| 0x22c, 0x22d | `case` :564, :565 | `cstat = 0` | :567 |
| 0x237 | `case` :562, `goto` caseD_227 (shared block) | deleted | :489 |
| 0x239 | `case` :576 | `cstat &= 0xfefe` | :580 |
| 0x23b | `case` :584 | `cstat &= 0xfefe` | :588 |
| 0x253 | `case` :592 | `cstat &= 0xfefe` | :594 |
| 0x25a | direct `if` :492 to caseD_204 | deleted, **gated** :494 | :513 |
| 0x25f | `case` :29, `goto` :30 to caseD_204 | deleted, **gated** :494 | :513 |
| 0x29d, 0x2a6, 0x2a8, 0x2ad, 0x2ae, 0x2b1, 0x2b6, 0x2b7, 0x2b9 | `case` :61-:69, caseD_29d | deleted | :96 |
| 0x2cc, 0x2f1, 0x2fd, 0x300, 0x301, 0x306, 0x30b, 0x318 | `case` :98-:105, bare `break;` :106, shared block | deleted | :489 |
| 0x365 | :27, skips inner switch, shared block | deleted | :489 |
| 0x385, 0x386 | `case` :316, :317 | deleted, **gated** :318 and :320 | :326 |
| 0x3ba, 0x3bb, 0x3bc, 0x3bd, 0x3f4, 0x3f5, 0x3f6 | `case` :398-:404, bare `break;` :405, shared block | deleted | :489 |
| 0x3cb, 0x3cc, 0x3cd, 0x3ce, 0x3cf | `case` :411-:415 | deleted | :417 |
| 0x3d3 | `case` :419, caseD_3d3 | deleted | :439 |
| 0x3eb | `case` :450 | `cstat = 0`, **gated** :451 | :456 |
| 0x401 | `case` :459, caseD_401 | deleted | :463 |
| 0x459 | `case` :465 | `cstat &= 0xfefe` | :467 |
| 0x485, 0x486, 0x487, 0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d, 0x48e | falls past :111 and :314, shared block | deleted | :489 |
| 0x548, 0x54a, 0x54b | `case` :118-:120, caseD_548, falls into `case 0x549` :124 | deleted, **gated** :126 | :160 |
| 0x549 | `case` :124 | deleted, **gated** :126 | :160 |
| 0x54f | `case` :167, `goto` :174 to caseD_401 | deleted | :463 |
| 0xa1e | `if` :180-:188 | `xrepeat = 0`, removed from hitscan's width test | :181 |
| 0x1003 | `if` :190 | `cstat &= 0xfefe` | :192 |
| 0x115c | `if` :179, `goto` caseD_29d | deleted | :96 |
| 0x1171 | `if` :198, `goto` caseD_3d3 | deleted | :439 |
| 0x134f | `if` :199, `goto` caseD_548 | deleted, **gated** :126 | :160 |

Seventy picnums. `tbot_sprite_breakable` (tbot.h) is a flat membership test over exactly
these, sorted ascending, with the same per-entry citations; the suite pins every member, the
neighbours of every contiguous run, an exhaustive sweep over 0x150-0x1360 (every value in that
span that is not a member is false, which covers every gap between members too), and the
excluded picnums below by name.

`0xa1e` is in the table with a third mechanism besides delete and `cstat` clear:
`sprite[param_1].xrepeat = 0` at `:181`. The rule answers to EFFECT, not to mechanism: a zero-`xrepeat` sprite fails hitscan's own half-width test
(`(tilesizx[tilenum] * xrepeat) >> 1` == 0 when `xrepeat` is 0), so the next ray does pass it by,
which is exactly what membership means here. Twelve of the seventy carry a runtime gate (see
"the conditional members" below); the other fifty-eight, `0xa1e` included, are unconditional.

### The conditional members: when they can refuse

Four gates sit between a hit and the delete. All four are runtime state, not picnum, so they
cannot be decided from the picnum the ray reports -- `tbot_sprite_breakable` counts these
picnums as breakable and accepts that the engine can refuse the individual shot:

- `:494`, `sprite[param_2].extra != *actorscrptr[0xa23]` -- `0x204`, `0x205`, `0x25a`, `0x25f`.
  A per-shot damage check against SHOTSPARK1's own script constant. **Rarely refuses**: it is
  false only when the shot's damage is exactly the unmodified constant, and the engine adds a
  random component to a hitscan's `extra` before it gets here.
- `:126`, `0 < sprite[param_1].extra` after subtracting the shot's damage -- `0x548`, `0x549`,
  `0x54a`, `0x54b`, `0x134f`. A hit-point pool: these refuse every shot but the last, and the
  last one always comes.
- `:318` and `:320`, an attacker check plus `(krand() & 3) != 0` -- `0x385`, `0x386`. This one
  refuses **three shots in four** (the other three push the sprite instead). It is a member
  because a hitscan reaches the delete, not because it usually does.
- `:451`, `(sprite[param_1].cstat & 1) == 0` returns early -- `0x3eb`. Vacuous for this table's
  purpose: that bit is what makes the sprite block the ray in the first place, so a `0x3eb` the
  ray actually stopped on has it set.

### Handled but not passable: what the walk deliberately leaves out

- **Weapon-gated** -- `0x38c`, `0x38e`, `0x3b7`, `0x3d2`, `0x3de` (`:359`) and `0x38f` (`:396`)
  clear `cstat &= 0xfefe`, but only for an attacker picnum in `0x659`/`0x1a`/`0x3d5`/`0x686`/
  `0xa2d` (`:349`, `:353`, `:366`, `:370`). The player's hitscan attacker is SHOTSPARK1, picnum
  `0xa23` -- the very constant the caseD_204 gate reads at `:494` -- which is not in that set,
  so a hitscan returns at `:350`/`:354` having changed nothing. Explosions open these, bullets
  do not.
- **Tile swap only, sprite stays and still blocks** -- `0x268`, `0x26a`, `0x26b`, `0x3e2`,
  `0x3e3`, `0x3e4` (caseD_268, `:37-:60`), `0x233`-`0x236` (`:573`), `0x3c9` (`:407`), `0x3d5`
  (`:442`), `0x54e` (`:163`). Several of these turn into a tile that IS a member (`0x3c9`
  becomes `0x3eb`, `0x54e` becomes `0x54f`, `0x233`-`0x236` become `0x237`), so the second shot
  opens them -- but the picnum on the ray now is not passable, and the table answers about the
  picnum on the ray now.
- **`0x58c` (`:113`)** re-points `param_1` at its owner and falls into the badguy path; nothing
  is opened.
- Everything reaching `switchD_00510f1d_caseD_158` (`:201`) -- the `default:` arms of all three
  inner switches, and every picnum with no case at all. That is the badguy/damage path; it never
  deletes and never clears `cstat`.

## The fix

`tbot_when_aimbot` (tbot.h) has a fifth term, `rayHitsBreakable`: it only ever widens the
existing "blocked" refusal into a hold (a new reason, "breaking"), never touches "lock" (a ray
that lands on a live enemy) or "nolock" (no target, or no line of sight, tested first as always).
`tbot_cast` (tbot.cpp) returns the whole hit -- sprite, wall, sector and the hit point -- as
a `TbotHit`, so `tbot_tic`'s aimbot branch can read the hit wall's `cstat`/`overpicnum`/
`nextsector` (bounds-checked against the live `engine_numwalls()`) and the next sector's
`floorz`/`ceilingz` (bounds-checked against `engine_numsectors()`), or the hit sprite's picnum
when it is not a live enemy, and classify the obstacle through the two rules above. Crosshair
mode's own condition is unchanged: with no lock there is no reason to shoot a window at all.

The `tbot:` log line carries `hw=`/`wover=`/`wcstat=`/`spic=`/`brk=`, so a `reason=blocked` tic
names the exact wall or sprite the rule still refuses.

## Open questions

- Any obstacle the engine opens that the two rules do not yet list shows up as a
  `reason=blocked` tic naming its wall or sprite.
