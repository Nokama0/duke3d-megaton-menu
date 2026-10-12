# Fog, shade, HUD and view (duke3d.exe, ultrawide-patched): engine facts

Read from Ghidra (static). VA = RVA + 0x400000. Every site below is fingerprinted by
the bytes quoted; a mismatch at install time means the feature stays off.

Summary: with the default "AccurateLighting" Polymost renders shade and distance fog as OpenGL fog.
Fullbright needs three levers (`nofog`, `r_usetileshades`, `getshadefactor`) and GL_FOG disabled
first. Distance fog alone is removed by zeroing `ps.visibility` around `displayrooms`. The HUD,
weapon and crosshair are drawn by `displayrest`.

## A. How Polymost lights a frame in this build

`CONFIG_ReadSetup` (VA 0x004b3960) sets `r_usenewshading = 2` and `r_usetileshades = 1`
when the game's "AccurateLighting" option is on (the default), else 0 and 0. The static
initial values are 2 and 1 (VA 0x005b1aa0 int, VA 0x005b1aa4 int).

With new shading 2, both the per-surface SHADE and the distance FOG are rendered as OpenGL
fog. `fogcalc` (VA 0x00474bf0) computes, per polygon, from `f = globalvisibility *
((sector visibility + 16) & 0xff)`:

- `f != 0`: linear fog from `shade * -600 / f` to `(numpalookups - 1 - shade) * 600 / f`.
- `f == 0` and `shade < 1`: start 2.9e30, end 3e30: no fog at all.
- `f == 0` and `shade >= 1`: start -2.9e30, end `(numpalookups - 1 - shade) * 2.9e30 /
  shade`: a UNIFORM blend toward the fog colour equal to `shade / (numpalookups - 1)`.
  This is how a wall's shade is drawn when there is no distance fog.

With tile shades on, `fogcalc` halves the shade it is given for art tiles and the other
half comes from the texture: `our_texcache_fetch` (VA 0x00478c60) picks the texture
variant `[0x01ea9018] >> 3` when `r_usetileshades == 1`, else variant 0.
`getshadefactor` (VA 0x004786f0) returns 1.0 for art tiles when tile shades are on, else
`(numpalookups - clamp(shade)) / numpalookups`, and the polygon drawer multiplies the
vertex colour by it.

`fogcalc_old` (VA 0x00474d60, new shading 0 or 1) uses `gvisibility` (VA 0x00dd0c30,
double, written once per frame by `polymost_drawrooms` at VA 0x0047a5a7 from
`globalvisibility`) and clamps the density to at least 0.001; a zero visibility gives no
visible fog and no division by zero.

## B. The three fullbright levers

| Lever | Address | Static value | Effect when changed for a frame |
|---|---|---|---|
| `nofog` | VA 0x005e87ee, byte (RVA 0x001E87EE) | 0 | Gates every `glEnable(GL_FOG)`/`glDisable(GL_FOG)` the engine issues, but it does NOT touch GL_FOG's current state by itself (see the per-polygon and per-HUD-sprite toggling below). |
| `r_usetileshades` | VA 0x005b1aa4, int (RVA 0x001B1AA4) | 1 | 0 makes the texture cache use the unshaded variant of every tile. First use loads those variants once. |
| `getshadefactor` | VA 0x004786f0 (RVA 0x000786F0), prologue `55 8B EC 83 EC 1C 0F BF 05 9C 70 EF 01` | code | Patched to `D9 E8 C3` (`fld1; ret`, cdecl, caller cleans) it returns 1.0 for every polygon. Restored after the frame. |

All three together are fullbright: no fog, no fog-shade, unshaded textures, white vertex
colour. `nofog` is the same variable `baselayer_setupopengl` (write at VA 0x0041e03f) sets
when a driver's fog is broken, so the renderer already handles the value.

**`nofog`'s per-polygon and per-HUD-sprite toggling.** Every
`glEnable(GL_FOG)`/`glDisable(GL_FOG)` in the engine is gated `if (!nofog)`, and the
engine toggles GL_FOG PER POLYGON and PER HUD SPRITE, not once per frame:

- `drawpoly`: `if (!nofog) glDisable(GL_FOG)` at VA 0x00476961 and 0x00476bfe, `if (!nofog)
  glEnable(GL_FOG)` at VA 0x00476b00 and 0x00476d2e.
- `polymost_dorotatesprite` (every HUD sprite, drawn by `displayrest` AFTER
  `displayrooms`): `if (!nofog) glDisable(GL_FOG)` at VA 0x0048585a before the sprite and
  `if (!nofog) glEnable(GL_FOG)` at VA 0x00485892 after it, so the HUD LEAVES GL_FOG
  ENABLED at the end of every frame, with that frame's own fog parameters.
- `polymost_drawalls`: `if (!nofog) glEnable(GL_FOG)` at VA 0x0047dc3a and 0x004801ac,
  re-enabling fog after the parallax sky (drawn with fog disabled).

Because of this, `nofog = 1` is only correct for a frame if GL_FOG is ALREADY disabled
when that frame starts: the engine's own `nofog = 1` mode works from a cold GL context,
where GL_FOG has never been enabled. Setting `nofog = 1` for one frame in the middle of a
run freezes whatever state the previous frame's HUD left GL_FOG in (enabled, stale
parameters), and that frame draws everything, sky included, under that stale fog. The
rule: **`nofog = 1` is valid for a frame only if GL_FOG is disabled first; the HUD leaves
it enabled.**

**The engine's fog loader pointers**, `void (__stdcall*)(unsigned)` (APIENTRY: the engine's call sites do no stack cleanup after them; a cdecl call corrupts ESP by 4), filled at GL init and
refilled on a context recreate:

| Pointer | Address |
|---|---|
| `bglEnable` | VA 0x01bff49c |
| `bglDisable` | VA 0x01bff4f8 (RVA 0x017FF4F8) |

The light bracket reads `bglDisable` fresh every armed frame and
calls it with `GL_FOG_ENUM` (`0x0B60`) itself, before the code patch arms, so the frame
starts from the same disabled GL_FOG state the engine's own `nofog` mode assumes rather
than inheriting whatever the previous frame's HUD left behind. A null pointer skips
arming fullbright for that frame only (logged once); no re-enable is needed on release,
because the next frame's own first polygon (or the HUD) re-establishes GL_FOG's state
through its own `glEnable`/`glDisable` pair once `nofog` is back to 0.

`CONFIG_WriteSetup` (VA 0x004b40a0) reads `AccurateLighting` (VA 0x005cb12c, the option
variable) and never `r_usetileshades` (writers: `CONFIG_ReadSetup`, `DidChangeOptionValue`
only), so a frame-scoped `r_usetileshades = 0` cannot reach the game's cfg even on an exit
taken mid-frame.

Accepted transient: the bracket disables GL_FOG on an armed frame and never re-enables it;
the first non-armed frame after fullbright is switched off draws fog-free until the engine's
own per-polygon `glEnable(GL_FOG)` fires, which happens on that frame's first fogged polygon.

## C. Distance fog alone (the free camera's "Remove fog")

`displayrooms` (VA 0x004c0ae0) copies the player's `visibility` (player_struct +616, long)
into the engine's `visibility` global (VA 0x01ed6444) at VA 0x004c0ba7 (`89 0D 44 64 ED
01`) at its start; `drawrooms` turns that into `globalvisibility` (VA 0x01dcf600) at VA
0x00429718..0x00429775 by one of three formulas, every one of which maps 0 to 0. A frame
whose `ps.visibility` is 0 therefore draws with `f == 0` in `fogcalc`: no distance fog,
shade intact (section A). The camera bracket ([freecam-accuracy.md](freecam-accuracy.md)
section A) substitutes player fields around `displayrooms` and restores them; `visibility`
joins that set.

## D. The HUD, weapon and crosshair (screenshot mode)

`displayrest` (thunk VA 0x004036bb `E9 80 B2 0B 00`, body VA 0x004be940; RVA 0x000036BB)
draws, in this order: `displayweapon(screenpeek)` when `ps.newowner < 0` and
`ud.camerasprite == -1` and `ud.overhead_on != 2` (call at VA 0x004bed86, the only caller),
then `displaymasks`, `moveclouds`, the overhead map, `coolgaugetext(screenpeek)` (the
STATUS BAR and mini HUD; thunk VA 0x0040162c, body VA 0x004bc0e0; its bar drawing is gated
on `ud.screen_size > 3`), `operatefta`, the crosshair (`rotatesprite` of tile 0x9db when
`ps.newowner == -1 && ud.overhead_on == 0 && ud.crosshair != 0 && ud.camerasprite == -1`),
then `menus()`.

- `displayweapon` thunk VA 0x004035b7 `E9 C4 3B 0F 00` (RVA 0x000035B7), body VA 0x004f7180.
- `displaymasks` thunk VA 0x00401852 `E9 59 52 0F 00` (RVA 0x00001852), body VA 0x004f6ab0;
  called from displayrest only, inside the same branch as displayweapon and additionally
  gated on over_shoulder_on == 0.
- `vscrn` thunk VA 0x004032a1 (RVA 0x000032A1), body VA 0x00506530, `void __cdecl
  vscrn(void)`: clamps `ud.screen_size` to 0..64, calls `flushperms()` when it is 0, and
  calls `setview` with the window for that size (full screen at 0, the bar's height
  subtracted at 8 and above), then sets `pub = pus = 1`. It is what the game itself calls
  when the size changes.
- `ud` VA 0x01b64040 (RVA 0x01764040), `user_defs`, 2412 bytes: `overhead_on` +1396
  (char), `camerasprite` +1404 (short), `screen_size` +1508 (long), `crosshair` +1516
  (long). `CONFIG_WriteSetup` saves `screen_size` and `crosshair` to the game's cfg, so
  both must be restored before the game can exit.
- `player_struct` (940 bytes): `visibility` +616 (long), `newowner` +708 (short), `gm`
  +844 (char; bit 0 set while the game's menu is open).

## E. Not levers

- `visibility` global (VA 0x01ed6444) cannot be substituted from outside: `displayrooms`
  overwrites it from the player field on entry (section C), so the player field is the one
  to substitute.
- `parallaxvisibility` (sky) and `globalshade` (67 writers, per wall) are untouched.
- Polymer (`PT_GetHead` reads `r_usetileshades`) is not the renderer in use.
