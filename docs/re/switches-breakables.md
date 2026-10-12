# Switches, cracks, breakable walls, dying enemies and struct layouts: engine facts

How the engine decides which switches are locked or combination-solved, which sprites are
explodable cracks, which walls break, how a dying enemy shows in its action record, and the
`walltype`/`sectortype`/`player_struct`/`hittype` layouts the menu reads.
Method: Ghidra on the shipping `duke3d.exe` with its PDB. VA = RVA + 0x400000. Every picnum
below is read from the game's OWN switch statements, not from a names table; the names in
parentheses are the stock Duke 3D names for readability and are not load-bearing.

## A. Switches and locks: `checkhitswitch` (VA 0x0050ea80) 

Signature `uchar checkhitswitch(short snum, long index, uchar isSprite)`. Sprite switches
live on statnum 0 (`headspritestat[0]`); wall switches are walls whose `picnum` is in the
set. A switch with `lotag == 0` is ignored by the game. `lotag == -1` ends the level.

**Recognised switch picnums (the function's own case lists, sprite and wall alike):**

| Family | Base picnum (+1 = pressed frame) | Behaviour |
|---|---|---|
| Access (keycard) | 130 (ACCESSSWITCH), 170 (ACCESSSWITCH2) | `pal` selects the card: 0 blue -> `ps.got_access & 1`, 21 red -> `& 2`, 23 yellow -> `& 4`. Missing bit => the game shows quote 70/71/72 and refuses. **Lock state = the player's `got_access` bit for the switch's pal.** `ps.got_access` is at the offset the item ESP (esp-sprites.md) reads for keycards; `access_incs` (+ nonzero while the card animation plays). |
| Two-state | 132, 134, 136, 138, 140 (SLOTDOOR, LIGHTSWITCH, SPACEDOORSWITCH, SPACELIGHTSWITCH, FRANKENSTINESWITCH), 164 (DIPSWITCH2), 168 (DIPSWITCH3), 712 (ALIENSWITCH), 860, 862, 864 (three pull/hand switches), 1111, 1122 (POWERSWITCH1) | picnum toggles between base and base+1. |
| Multi-state | 146..149 (MULTISWITCH) | picnum cycles 146 -> 149 -> 146; the activated channel is `lotag + (picnum - 146)`. |
| **Combination** | 162 (DIPSWITCH), 166 (TECHSWITCH), 1142 (LOCKSWITCH1) | All members share a `lotag`. **The required state of each member is its `hitag`: 0 = must be on the base frame, 1 = must be on base+1.** The game counts members whose frame matches their hitag (`local_10`) against the member count (`local_18`); the channel fires only when they are equal. So "needs flipping" for a member = `((picnum - base) & 1) != hitag`. This is the whole combination solver. |

Everything else falls to `isadoorwall(picnum)`: a wall or sprite whose picnum is a DOOR TILE and whose `lotag != 0` is ALSO operated by `checkhitswitch` (the "secret switches that behave differently": pressing a door-textured wall fires its lotag channel, with no switch frame to flip). **`isadoorwall` (VA 0x0050be30) returns true for picnums 150..159 (0x96..0x9f), 395 (0x18b), 447..449 (0x1bf..0x1c1), 717 (0x2cd), 781 (0x30d), 1102 (0x44e), 1144 (0x478), 1169 (0x491), 1178..1179 (0x49a..0x49b), 4391 (0x1127)** and false for everything else. Family SW_DOOR, label "Usable", no second line.

**Keycards are never consumed in this build:** every writer of `ps.got_access` (VA 0x019cbe3c = ps[0] + 732, xrefs) is an OR of 1, 2 or 4 in the CON interpreter `parse` (VA 0x004e4576 / 0x004e45a3 / 0x004e45d0, the card pickup), plus `resetplayerstats` and `cheats`; `processinput` never clears a bit. So the bit stays set after the lock is opened. **An opened access switch sits on its pressed frame (131 / 171) for the rest of the level; the ESP must show no card line on the pressed frame.**

## B. Explodable cracks: `movestandables` (VA 0x0049b230)

Not cracks: picnums 908, 910, 951, 978, 990 are the game's destructible PROPS (the box, box, palm tree, cone and tire family: `checkhitsprite` cases 0x38c/0x38e/0x3b7/0x3d2/0x3de react to explosives with `t[0] = 1` and movestandables shrinks and deletes them). They look like explodables but are not marked "Explodable" by the menu.

**The real cracks are picnums 546..549 (0x222..0x225, CRACK1..CRACK4)**, wall-aligned sprites on statnum 6, read from `movestandables`: `if (picnum in 546..549) { if (hitag > 0) { t0 = cstat; t1 = ang; j = ifhitbyweapon(i); if (j in {0x394 FIREEXT, 0xa2d RPG, 0x686, 0x4df SEENINE, 0x437 OOZFILTER}) { every statnum-6 sprite with the same hitag and picnum 0x437/0x4df gets shade -32; goto DETONATE } else { cstat = t0; ang = t1; extra = 0; } } }` (the branch attached to `if (picnum < 0x233 || picnum > 0x236)` at the top of the standables loop; 0x233..0x236 is WATERFOUNTAIN). DETONATE spawns EXPLOSION2 (0x762), runs `hitradius`, and `deletesprite`s the crack. **A crack only detonates when `hitag > 0`** (the hitag links it to the barrels and effectors it fires); a crack with hitag 0 is inert decoration and must not be marked. **Liveness = the sprite still exists on statnum 6** (the game deletes it on detonation); `temp_data[0]` is NOT a flag here (it is scratch for cstat).

Explosive barrels, unchanged: 1079 (OOZFILTER), 1247 (SEENINE), 1238 (EXPLODINGBARREL); not in scope.

## C. Breakable walls: `checkhitwall` (VA 0x0050fcd0) 

Walls whose `picnum` the game breaks with `breakwall(newpic, ...)` (destructible panels):
293, 297, 299, 301, 305, 306, 307 (W_TECHWALL1/2/3/4/10/15/16), 4130..4133 (W_TECHWALL11..14),
4134, 4136, 4138, 4140, 4142 (W_TECHWALL5..9), 4181 (W_MILKSHELF), 1212 (COLAMACHINE), 1215
(VENDMACHINE). Once broken the picnum changes (to 360..363, 4144..4147, 4203, or +1/+2), so
"not yet broken" = picnum still in the set. Glass/screen walls (263..275, 806, 1289, 1298,
3370, 4120, 4123, 4125, 4127..4129 -> random 357..359) and light walls (120, 122, 124, 701,
703, 705) are shootable decoration, not explodables. Masked-wall extras by
`overpicnum`: 503 GLASS, 510 STAINGLASS1, 407 FANSPRITE, 663..665 W_FORCEFIELD, 560 MIRROR.

## D. Dying enemies: the current CON action

The menu's `aim deathstate:` log lines (112 of them in one capture): dying LIZTROOP (1680) show
`dispicnum` 1730..1733 with `t4 = 36180108`; alive-at-zero-HP troopers show `dispicnum` 1715
(shooting), 1739, 1740 or 1680 with `t4 = 36180188` or `36180028`; dying PIGCOP (2000) show
`dispicnum` 2055/2056 with `t4 = 36192916`. `t4 = hittype[i].temp_data[4]` is the CON
interpreter's **pointer to the current action's data in the compiled script**: five longs,
`+0 startframe, +4 numframes, +8 viewtype, +12 incval, +16 delay` (the three observed
values differ by exactly 80 bytes = four five-long actions, consistent with that layout).
In the stock CON, death actions are declared with **viewtype 1** (one view, no rotation:
`action ATROOPDYING 50 5 1 1 16`), while walking, shooting and ducking actions are viewtype 5.
A viewtype-1 test alone (`*(long*)(t4 + 8)` == 1 at `extra <= 0`) separates dying from
alive-at-zero in that capture, but the menu does not rely on it: enemy state is classified from
`extra`, cstat bit 8 (hittable) and known action records (actorstate.h). Those records are
`ATROOPSUFFERING` (59, 2, 1, 1, 21), the `ATROOPDEAD`/`ATROOPPLAYDEAD` pair (54, 0, 0, 0, 0),
`SUFFERDEAD` (58, 2, 1, -4, 24) and the Assault Commander's death record (30, 8, 1, 1, 12).
**Where the script lives:** the PDB has a static `long script[23056]` at VA 0x019d94e0 (ends 0x019efd20), but the logged action pointers (36180028..36193032 = 0x2281xxx) and the move pointers logged as `t1` (= `temp_data[1]`, also absolute) lie far outside it and outside the image Ghidra loaded (28.8 MB from 0x400000). This build compiles the CON into a heap buffer; `scriptptr` (VA 0x01b61004) is only the compile cursor. So the bounds are not knowable statically: read the action only when `VirtualQuery` reports the 20 bytes committed and readable, then apply a plausibility gate (viewtype in {1,3,5,7,8}, numframes 1..64, startframe 0..1023). `actorscrptr[picnum]` (VA 0x01b575a0, the max-HP source) holds pointers into the same buffer, which a diagnostic can print beside `t4` to confirm they share a region.

Read `t4` as an address; dereference only under the VirtualQuery-plus-plausibility rule above.

## E. Nearest-sector seeding for the camera

When `updatesectorz` finds no sector for the camera (out of bounds), the held sector seeds
the renderer's portal walk (freecam-accuracy.md, section G). A better seed: the sector whose walls
are nearest to the camera in 2D (minimum over walls of point-to-segment distance, ties broken
by whether the camera z lies within the sector's floor/ceiling span). Pure function over the
`wall`/`sector` arrays the ESP already reads; O(numwalls) per tic, run only while the
updatesectorz result is -1. Toggle `freecamnearest` (default 0) chooses between "hold the
last valid sector" and "seed from the nearest".

## F. `ud.pause_on`, hitscan, scansector, clamps -- see freecam-accuracy.md, sections F and G.

## G. Struct offsets the menu reads (PDB layouts)

`walltype` (32 bytes, matches `rva::WALL_STRIDE`): `x` +0 (long), `y` +4 (long), `point2` +8
(short), `nextwall` +10, `nextsector` +12, `cstat` +14, `picnum` +16, `overpicnum` +18,
`shade` +20 (schar), `pal` +21 (char), `xrepeat` +22, `yrepeat` +23, `xpanning` +24,
`ypanning` +25, `lotag` +26 (short), `hitag` +28 (short), `extra` +30 (short).
Cross-checks against the binary: `checkhitwall` reads `wall[i].picnum` at +16 and
`wall[i].overpicnum` at +18 for its case lists; `checkhitswitch` reads `wall[i].lotag` +26
and `wall[i].hitag` +28 (its `local_24`/`local_2c`); `polymost_scansector` reads
`wall[i].point2` +8 and `nextsector` +12. `wall[]` base RVA 0x018D18E0, `numwalls` short at
VA 0x01dd4b5c (RVA 0x019D4B5C, already `rva::NUMWALLS`); `wall` is `walltype[16384]` at VA 0x01cd18e0.
`prelevel`'s wall loop is VA 0x005079f8-0x00507a0e; its `numwalls` read is VA 0x005074ae-0x005074d4.

`sectortype` (40 bytes, matches `rva::SECTOR_STRIDE`): `wallptr` +0 (short), `wallnum` +2
(short), `ceilingz` +4 (long), `floorz` +8 (long), `ceilingstat` +12, `floorstat` +14,
`ceilingpicnum` +16, `ceilingheinum` +18, `ceilingshade` +20, `ceilingpal` +21,
`floorpicnum` +24, `floorheinum` +26, `floorshade` +28, `floorpal` +29, `visibility` +32,
`lotag` +34 (short), `hitag` +36 (short), `extra` +38 (short).

`player_struct` (940 bytes): `got_access` **+732** (short; bits 1 blue, 2 red, 4 yellow,
tested by `checkhitswitch` at VA 0x0050ebc7..0x0050ec7c), `access_incs` +720 (short),
`access_wallnum` +726, `access_spritenum` +728, `kickback_pic` +730, `curr_weapon` +696,
`gotweapon` +893 (char[12]), `dead_flag` +812, `on_ground` +850, `jetpack_on` +885.
Already in `common.h`: `posx/y/z` 532/536/540, `horiz` 544, `oposx/y/z` 568/572/576,
`opyoff` 584, `poszv` 596, `ang` 652, `oang` 654, `cursectnum` 658, `look_ang` 660,
`horizoff` 702, `newowner` 708, `over_shoulder_on` 746, `orotscrnang/rotscrnang` 808/810,
`spritebridge` 886.

`hittype` (72 bytes): `temp_data[0..5]` at +48 .. +68 (`rva::HITTYPE_TEMPDATA` 48), so the
current-action pointer `temp_data[4]` is +64 and the move pointer `temp_data[1]` is +52. `floorz` is at +20 (confirmed in `makeitfall`, VA 0x004dd120).
