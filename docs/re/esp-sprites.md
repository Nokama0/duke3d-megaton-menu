# ESP engine facts: enemies, sprites, projection

Resolved from the shipping `duke3d.pdb` (static) unless marked. VA = RVA + 0x400000.

Summary: enemy tests call the engine's `badguy`; sprites are walked through the status lists; the
world-to-screen projection reads Polymost's own nine per-frame `double` constants, never the camera
globals; the FOV slider's number is the true horizontal FOV; no max-health table exists.

## Enemy discrimination: call the engine, do not reimplement

Reading `actortype[]` directly would be WRONG: the engine's own test is a picnum switch with
hardcoded exceptions AND an `actortype[]` fallback. Decompiled:

```c
short __cdecl badguy(spritetype *spr) {
  int pic = spr->picnum;
  // hardcoded accepts: 0x2A3, 0x4F3, 0x60E, 0x690..0x6CC, 0x6B3, 0x6BD,
  //                    0x6D0, 0x71C, 0x758, 0x780, 0x781, 0x7A8, 0x7FD,
  //                    0x848, 0x866, 0x870, 0x875, 0x938, 0x974, 0xA46,
  //                    0xA96, 0xAC8, 0x1284 (ranges per the switch)
  // fallback:
  return actortype[pic] != 0;
}
```

So ESP CALLS the engine function. Reading `actortype[]` alone misses every hardcoded case.

| Symbol | Thunk VA | Thunk RVA | Real VA | Real RVA | Signature |
|---|---|---|---|---|---|
| `badguy` | 0x00403BCF | 0x00003BCF | 0x004B9C50 | 0x000B9C50 | `short __cdecl (spritetype*)` |
| `badguypic` | 0x00404200 | 0x00004200 | 0x004B9EC0 | 0x000B9EC0 | `short __cdecl (short picnum)` |

Call them directly by address; no patching needed. `badguypic` is the cheaper form when only the
picnum is in hand.

`badguy` also returns 1 for the GAME.CON `useractor enemy` entries (CANNON 1810, TANK 1975, the
NEWBEAST family) through `actortype[]`. `HEAVYHBOMB` (26) is both the pipebomb-box pickup and every
live thrown pipebomb; only `hittype[].temp_data` tells them apart (VA 0x004a3d3f, which the menu
does not read).

## Sprite iteration and data

| Symbol | VA | RVA | Type |
|---|---|---|---|
| `sprite` | 0x01DD4B60 | 0x019D4B60 | `spritetype[16384]`, stride 44 |
| `headspritestat` | 0x01C907C0 | 0x018907C0 | `short[1025]` |
| `nextspritestat` | 0x01E91760 | 0x01A91760 | `short[16384]` |
| `sector` | 0x01C93F80 | 0x01893F80 | `sectortype[4096]`, stride 40 |
| `numsectors` | 0x01C907A0 | 0x018907A0 | `short` |
| `actortype` | 0x01B18900 | 0x01718900 | `char[9500]` |

`spritetype` fields: x +0x00, y +0x04, z +0x08 (longs); cstat +0x0C, picnum +0x0E
(shorts); xrepeat +0x14, yrepeat +0x15 (uchars); sectnum +0x18, statnum +0x1A,
ang +0x1C, owner +0x1E, xvel +0x20, yvel +0x22, zvel +0x24, lotag +0x26,
hitag +0x28, **extra +0x2A** (all shorts). `extra` is current HP for enemies.

Walk the status lists rather than scanning 16384 slots.

`sectortype`: wallptr +0x00, wallnum +0x02 (shorts); floorz +0x08; lotag +0x22,
hitag +0x24, extra +0x26 (shorts). **Secret sector = `lotag == 32767`**; the engine clears it on
discovery ([damage-triggers-weapons.md](damage-triggers-weapons.md) section B), so a secret that
is still marked has not been found.

## Projection

The engine exposes no world-to-screen helper. **Do NOT rebuild Build's maths
from the camera globals.** Read Polymost's own per-frame constants instead:
`polymost_drawrooms` (entry 0x0047a2d0) writes these nine once per frame, and
every polygon that frame, including the face sprites the ESP marks, is
transformed by exactly them. All nine are C `double` per the shipping PDB; `gtang` (a float) is not among them.

| Symbol | VA | RVA | written at | value |
|---|---|---|---|---|
| `gyxscale` | 0x00DD0C38 | 0x009D0C38 | 0x0047a4dd | `xdimenscale / 131072.0` |
| `gcosang` | 0x01B74350 | 0x01774350 | 0x0047a51f | `cosglobalang / 262144.0` |
| `gsinang` | 0x01B732D0 | 0x017732D0 | 0x0047a531 | `singlobalang / 262144.0` |
| `gcosang2` | 0x01B73280 | 0x01773280 | 0x0047a549 | `viewingrange * gcosang / 65536.0` |
| `gsinang2` | 0x01B74340 | 0x01774340 | 0x0047a561 | `viewingrange * gsinang / 65536.0` |
| `ghalfx` | 0x00E14C58 | 0x00A14C58 | 0x0047a56d | `(double)halfxdimen` |
| `ghoriz` | 0x00E14C60 | 0x00A14C60 | 0x0047a638 | `(double)(ydimen>>1)`, overwriting 0x0047a58f |
| `gshang` | 0x01B73290 | 0x01773290 | 0x0047a5fa | `dh / sqrt(ghalfx^2 + dh^2)` |
| `gchang` | 0x01B73288 | 0x01773288 | 0x0047a61e | `sqrt(1 - gshang^2)` |

Nothing clobbers them between the 3D pass and buffer swap: `get_xrefs_to` on
each gives only `polymost_drawrooms` and `polymost_glreset` (0x00474a4d, the
GL context reset, not per frame) as writers. `polymost_dorotatesprite`, the 2D
HUD pass that runs after the 3D scene, only reads them.

Still needed from the camera globals, for the position and the viewport origin
(these are not part of the g-block):

| Symbol | VA | RVA |
|---|---|---|
| `globalposx` | 0x01EDC9A4 | 0x01ADC9A4 |
| `globalposy` | 0x01D67DCC | 0x01967DCC |
| `globalposz` | 0x01C93F20 | 0x01893F20 |
| `windowx1` | 0x01DD2624 | 0x019D2624 |
| `windowy1` | 0x01C93F2C | 0x01893F2C |
| `sintable` | 0x01E8B020 | 0x01A8B020 (short[2048]) |

`globalang` (0x01DD4B5E / 0x019D4B5E, short), `globalhoriz` (0x01CD18BC /
0x018D18BC), `halfxdimen` (0x01ED6440 / 0x01AD6440), `xdimenscale`
(0x01C93F30 / 0x01893F30) and `viewingrange` (0x01EF7094 / 0x01AF7094) are correct
addresses but **diagnostic inputs only**, for the reason below. `ydimenscale` (0x01C93F5C /
0x01893F5C) is read solely by the 2D HUD-sprite rotator and has no part in the 3D projection.

### Why the projection reads the g-block, not `viewingrange`

Recomputing the projection from the camera globals is *algebraically identical* to the engine
form: substitute `gcosang = cosglobalang/262144` and
`gcosang2 = viewingrange*gcosang/65536` and the two expressions are the same. Checking the
arithmetic therefore proves nothing. The defect is the SOURCE of `viewingrange`: the value that
global holds at buffer swap, when an overlay samples it, is not the value
`polymost_drawrooms` consumed for the frame being drawn over. Three writers can
separate them within one frame:

* `displayrooms` 0x004c0d84 calls `setaspect`, but only inside a change-detect
  against its own cached copy at 0x005cb574, so on a steady frame it is not
  called at all;
* `displayrooms` 0x004c0fed calls `setaspect` again for the drunk/shrooms
  screen wobble;
* `polymost_dorotatesprite` 0x00484505 forces `viewingrange` to 65536 for the
  2D HUD pass and restores it at 0x00484abf.

Method: an overlay drawing three candidate projections at once, each in its own colour with its
own letter, settled which one matches. When a projection or a scale is in doubt, draw the
candidates; and prefer reading a renderer's own per-frame state over reconstructing it, even when
the reconstruction provably matches the source.

## Max HP for the health bar

No max-health table was found. Enemies spawn at full health, so the menu records the first
`extra` seen per picnum as that type's maximum, and shows the raw current value
when no maximum is known yet. It does not fabricate a denominator.

## FOV slider

Measured from the ESP diagnostic's engine g-constants with the slider at 120:
`gcosang2/gcosang = 1.7320` (sinang pair 1.7321) vs `tan(60 deg) = 1.7321`.
So the frame's horizontal half-FOV is exactly the slider's label / 2: **the
slider's number is the true on-screen HORIZONTAL FOV**, at any aspect. At 90
the factor is 1.0 (settled by the three-candidate overlay above), so stock Megaton on
3440x1440 renders 90 deg horizontal (Vert-), not Hor+.

The `viewingrange` global reads 117418 while the frame used 65536; this is not a bug: the
global carries the display aspect factor (1.7917) and the hook's substituted value
(203374 = 117418 x tan60) is what `setaspect` sticks; Polymost's per-frame factor comes out as
tan(fov/2) regardless. Reading the global to reconstruct the frame's FOV is wrong, and nothing
does that.

**Rule for aim code:** never read `g_cfg.fov` or `viewingrange`.
Screen<->world conversions use `engine_gproj()` (gcosang2/gsinang2 carry the
FOV), exactly as `project_world` does. FOV then cannot desync the aim.

## Open questions

- Whether scaling EVERY `setaspect` call also scales the HUD pass: `dorotatesprite` reads
  `xdimenscale`, which does not depend on `viewingrange`, and no HUD distortion has been seen at 120.
