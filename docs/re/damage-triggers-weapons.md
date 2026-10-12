# Damage, triggers, weapon frames (duke3d.exe, ultrawide-patched): engine facts

Read from Ghidra (static; the Ghidra project's `/duke3d.exe`, PDB names). VA = RVA + 0x400000. Every
hook site below is fingerprinted by the bytes quoted; a mismatch at install time means the feature
stays off. Struct offsets are quoted from the instructions that use them, not from headers.

Summary: all damage to every sprite passes through `ifhitbyweapon`; hidden triggers are statnum 6 and
8 sprites with known picnums; weapon cadence is a per-weapon state machine on `kickback_pic` with a
small set of event values the fire-rate advance must not skip.

## A. Damage: `ifhitbyweapon`, the one place an actor's health goes down

`short __cdecl ifhitbyweapon(short i)`, body VA 0x00499a00, thunk VA 0x00403e9f (RVA 0x00003E9F)
with bytes `E9 5C 5B 09 00` (JMP to the body). Every caller goes through the thunk: `parse`
(the CON interpreter's `ifhitweapon`, VA 0x004e22e9, which is how every scripted enemy takes
damage), `moveactors` (seven sites, the hard-coded actors), `movestandables` (six sites, props
and cracks), `movefallers` and `moveeffectors`. Hooking the thunk therefore sees every
application of damage to every sprite.

What it does (decompiled, VA 0x00499a00): if `hittype[i].extra < 0` or `sprite[i].extra < 0`,
clears `hittype[i].extra` to -1 and returns -1 (no hit this tic). For a non-player victim
(`sprite[i].picnum != 0x57d` APLAYER): a zero-damage shrinker spark (`hittype.picnum == 0x66e`
with `xrepeat < 24`) returns -1 without touching health; otherwise
`sprite[i].extra -= hittype[i].extra`, the owner may be transferred, then
`hittype[i].extra = -1` and the weapon picnum is returned. So the damage applied this tic is
`hittype[i].extra` READ BEFORE the call, and the health after is `sprite[i].extra` READ AFTER it.
Damage is already summed per tic: `checkhitsprite` (VA 0x00510ec0) accumulates every hit on the
victim into `hittype[i].extra` and stores the damaging sprite's owner, so seven shotgun pellets
arrive here as one number.

The per-actor record `hittype[]` (base VA 0x019f2880, RVA 0x015F2880 = `rva::RVA_HITTYPE`,
stride 0x48 = 72 = `rva::HITTYPE_STRIDE`), the fields this function touches, all `short`:

| Field | Offset | Instruction |
|---|---|---|
| `picnum` (the weapon) | +2 | `MOVSX EAX, word ptr [EDX + 0x19f2882]` |
| `ang` (hit direction) | +4 | `MOVSX ECX, word ptr [ECX + 0x19f2884]` |
| `extra` (damage this tic, -1 = none) | +6 | `MOVSX ECX, word ptr [EAX + 0x19f2886]` |
| `owner` (sprite index of the shooter) | +8 | `MOV CX, word ptr [EAX + 0x19f2888]` |

`sprite[]` (base VA 0x01dd4b60, stride 44): `picnum` +14, `xrepeat` +20, `statnum` +26,
`owner` +30, `yvel` +34, `extra` (health) +42, all as engine.h's `SPR_*` constants already say.
The local player's own sprite index is `ps[].i` at +742 (`rva::PS_I`); `ps[]` base VA
0x019cbb60, stride 0x3ac = 940. Splash from the player's explosions carries the player as
owner because the explosion sprite's owner is the player.

## B. Hidden triggers: touchplates, activators, master switches

Lists and picnums, from `operateactivators` (VA 0x0050e460), `operatemasterswitches` (VA
0x0050e8e0) and the touchplate block of `movestandables` (VA 0x0049b230):

| Thing | statnum list | picnum | Where it says so |
|---|---|---|---|
| activator (operates its own sector) | 8 | any but 4 | `operateactivators` walks `headspritestat[8]` |
| locked activator (toggles its sector's lock) | 8 | 4 (ACTIVATORLOCKED) | `if (sprite[j].picnum == 4)` at 0x0050e6xx |
| master switch (delayed activator) | 6 | 8 (MASTERSWITCH) | `operatemasterswitches` walks `headspritestat[6]`, `picnum == 8` |
| touchplate | 6 | 3 (TOUCHPLATE) | the `movestandables` block below; `CMP EAX, 0x3` at 0x0049d935 |

`operateactivators(lotag, player)`: for every statnum-8 sprite with `sprite.lotag == lotag`: if
`picnum == 4` it toggles bit 0x4000 of `sector[sprite.sectnum].lotag` (locked / unlocked, with
the "locked"/"unlocked" message when a player did it) and does nothing else; otherwise, subject
to `sprite.hitag` (1 = only when the sector is closed, `floorz == ceilingz`; 2 = only when open;
anything else = always), it toggles the sector effectors 18, 31, 32 and 36 inside sectors with
`lotag < 3` and calls `operatesectors(sprite.sectnum, j)`. Then `operaterespawns(lotag)`.
Cyclers with that lotag are toggled first. `operatemasterswitches(lotag)`: every statnum-6
sprite with `picnum == 8`, that lotag and `yvel == 0` gets `yvel = 1`, which starts its countdown
(its sector is operated `hitag` tics later by `movestandables`).

The touchplate rule, `movestandables` 0x0049d7ff-0x0049d8fb, with `t` = the sprite's
`hittype.temp_data` (+48, six longs):
`if (t[5] == 1) skip` (exhausted); `p = checkcursectnums(sprite.sectnum)` (VA thunk 0x0040269e:
the player standing in that sector, or -1); if `p < 0` -> `t[0] = 0`; else if
`!ps[p].on_ground` (+850) and `sprite.ang != 512` -> `t[0] = 0`; else if `t[0] == 0` and
`check_activator_motion(sprite.lotag) == 0` (thunk 0x00401be5: no sector with that lotag is
still moving) then `t[0] = 1; t[1] = 1; t[3] = !t[3]; operatemasterswitches(lotag);
operateactivators(lotag, p); if (sprite.hitag > 0) { sprite.hitag--; if (sprite.hitag == 0)
t[5] = 1; }`. So: edge-triggered on entering the sector while on the ground (angle 512 plates
also fire in the air), `hitag` 0 means unlimited uses, `hitag` N means N uses and then `t[5]`
marks it dead. Visible switches reach the same `operateactivators` from `checkhitswitch`
(0x0050f789) and `activatebysector` (0x0050fc38).

A secret sector is `sector.lotag == 32767`, cleared by the engine on discovery
(`checksectors`, VA 0x00514180, called every tic: it reads `sector[cursectnum].lotag` at VA 0x005141ad,
compares with 0x7fff at VA 0x005141c7, then at VA 0x005141e4 writes `lotag = 0`, calls `FTA(9,p)` and
increments `p->secret_rooms` at player +0x3a0; so a found secret leaves the ESP with no found-set to keep)
([esp-sprites.md](esp-sprites.md)); `engine_sector_is_secret` and `engine_sector_lotag` (engine.h)
read it. Wall adjacency: `wall[w].nextsector` at +12 (`rva::WAL_NEXTSECTOR`), walls of sector s are
`sector[s].wallptr .. +wallnum` (shorts at +0 and +2 of the 40-byte sectortype,
[switches-breakables.md](switches-breakables.md) section G).

## C. Weapon frames: `processinput`'s state machine on `kickback_pic`

`kickback_pic` is `short ps[].kickback_pic` at +730 (`rva::PS_KICKBACK_PIC`,
[switches-breakables.md](switches-breakables.md)); `curr_weapon` at +696; the fire button is
`sync.bits & 4`. `processinput` (body VA 0x004fa450)
holds a pointer to the counter (`ADD ECX, 0x2da` at 0x004fa48c) and runs the block below once
per tic when the counter is non-zero. Read as: with the counter's INPUT value `v` at the start
of the tic, what the tic does; `k` is the value the hook sees after the original returns.

| Weapon (`curr_weapon`) | Per-tic behaviour, from the decompile | Event input values E |
|---|---|---|
| 0 mighty foot | `kb++; if kb==7 shoot; else if kb==14 { held ? kb = 1..4 random : kb = 0 }` | {6, 13} |
| 1 pistol | `if kb==1 shoot; else if kb==2 spawn shell; kb++; if kb>4 { if ammo<1 or ammo%12 != 0 -> kb=0; else if kb==5 sound; else if kb==8 sound }; if kb==27 kb=0` | {1, 2, 4, 7, 26} |
| 2 shotgun | `kb++; if kb==4 shoot x7; switch kb: 13 checkavail; 15 sound; 17,20 kb++ again; 24 spawn shell, kb++; 31 kb=0` | {3, 12, 14, 16, 19, 23, 30} |
| 3 chaingun | `kb++; if kb<13 { if kb%3==0 { shoot; if !held or ammo<1 kb=0 } } else if kb>10 { held ? kb=1 : kb=0 }` | {2, 5, 8, 11, 12} |
| 4 RPG | `kb++; if kb==4 shoot; else if kb==20 kb=0` | {3, 19} |
| 5 pipebomb | throw animation, `hbomb_hold_delay`, switches to 10 | none: excluded |
| 6 shrinker | `if kb<11 kb++; else { kb=0; shoot }` | {11} |
| 7 devastator | `if kb!=0 { kb++; if kb odd shoot; if kb>5 kb=0 }` | {2, 4, 5} |
| 8 tripbomb | placement, moves `posz` | none: excluded |
| 9 freezer | `if kb<4 { kb++; if kb==3 shoot; if xrepeat<32 or ammo<1 kb=0 } else { held ? kb=1 : kb=0 }` | {2, 3, 4} |
| 10 hand remote | detonator | none: excluded |
| 11 expander | `if kb<4 kb++; else { kb=0; shoot }` | {4} |

An event input value is a `v` at which the next tic does something other than `kb = v + 1`:
a shot, a sound or shell, a decision (reload or end) or a reset. The fire-rate advance may move
`k` forward to AT MOST the next event input value at or above it, and a `k` already sitting on
an event value is left alone: the engine then performs that event itself on the following
tic, so no shot, sound or reset is skipped and none is doubled, and landing exactly on the
value before a shot is what shortens the cadence. Lead-in frames
before a weapon's first shot compress under the same rule (they are plain increments); for the
shrinker and expander they are the only compressible frames there are. The second `switch`
(decompile lines 1407-1500) is the fire PRESS: it sets `kb = 1` from 0 when the button is held
and ammo is there; the advance never touches `kb == 0`.

Shot counts from the suite's simulation: 600 tics with the fire button held, the advance applied
after each engine step. The simulation restarts the sequence on the tic after a reset (the game
spends one more tic on the fire press), so in-game cadence is one tic slower per cycle.
The last column is the cycle length in tics, stock to the maximum advance.

| weapon | stock | 150 | 200 | 250 | 300 | 350 | 400 | tics/shot at 400 | cycle ceiling |
|---|---|---|---|---|---|---|---|---|---|
| 0 mighty foot | 46 | 67 | 86 | 100 | 120 | 150 | 150 | 4.0 | 14 to 4 |
| 1 pistol | 150 | 200 | 200 | 200 | 200 | 200 | 200 | 3.0 | 5 to 4 |
| 2 shotgun | 23 | 29 | 40 | 43 | 55 | 55 | 55 | 10.9 | 29 to 8 |
| 3 chaingun | 200 | 299 | 300 | 300 | 480 | 480 | 480 | 1.25 | 3 to 2 |
| 4 RPG | 32 | 43 | 60 | 67 | 75 | 86 | 100 | 6.0 | 20 to 3 |
| 6 shrinker | 54 | 75 | 100 | 120 | 120 | 150 | 150 | 4.0 | 11 to 2 |
| 7 devastator | 240 | 300 | 300 | 300 | 300 | 300 | 300 | 2.0 | 6 to 5 per two shots |
| 9 freezer | 150 | 150 | 200 | 200 | 200 | 200 | 200 | 3.0 | 4 to 3 |
| 11 expander | 150 | 200 | 200 | 200 | 300 | 300 | 300 | 2.0 | 4 to 2 |

## D. Player and sprite fields the menu reads or writes

| Field | Offset | Note |
|---|---|---|
| `ps.i` | +742 (`rva::PS_I`) | the player's sprite index, the owner filter |
| `ps.curr_weapon` | +696 (`rva::PS_CURR_WEAPON`) | selects the frame table |
| `ps.kickback_pic` | +730 | the only player field the fire-rate advance WRITES, on the tic clock after the original |
| `ps.on_ground` | +850 | read by the touchplate rule (documentation only) |
| `sprite.lotag`, `.hitag`, `.statnum`, `.sectnum`, `.picnum`, `.extra` | +38, +40, +26, +24, +14, +42 | engine.h `SPR_*` |
| `hittype.temp_data[5]` | +48 + 20 | the touchplate's "used up" flag |

## E. The panel's drawing surface

`canvas.cpp` draws with fixed-function GL 1.x inside one `glPushAttrib(GL_ALL_ATTRIB_BITS)` /
`glPopAttrib` pair per frame: `glBegin(GL_QUADS)` fills, `GL_LINES`, `glColor4ub`, alpha
blending `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, one 2D texture for the font atlas, `glScissor`
for the content clip. The overlay draws from the `SDL_GL_SwapWindow` hook after the game's
frame, so the back buffer holds the finished scene when the panel is drawn: a
`glCopyTexSubImage2D` of the panel's rectangle captures the scene behind it. No shaders, no
framebuffer objects, no stencil are assumed. Every primitive has a `NO_GL` twin that records a
`CanvasOp` so the suite's pixel sweeps can see what was drawn.
