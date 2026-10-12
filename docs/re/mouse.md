# The mouse path (duke3d.exe, ultrawide-patched): engine facts

Read from Ghidra (static). VA = RVA + 0x400000. Every hook site below is fingerprinted
by the bytes quoted; a mismatch at install time means the feature stays off.

Summary: raw counts come from ManyMouse (or the SDL fallback), are scaled and truncated in
`dnOverrideInput` into `loc.avel`/`loc.horz`, and are applied by `processinput` in coarse units
(2 angle units per `avel`, 2 `horz` per horizon unit). The renderer derives its view from doubles
and integers written in `polymost_drawrooms`; the mouse aim overrides them from a fractional
angle and horizon at the `polymost_scansector` call site, rebuilds the screen clip polygon, and
leads the mirror pass with the lead's sign flipped.

## A. Raw counts

Two sources, chosen once at startup by `s_numDevices` (VA 0x005d3054, int; the ManyMouse
device count):

- **ManyMouse (raw input), used when `s_numDevices > 0`.** `dnUpdateMouseInput` (body VA
  0x00407e20, thunk VA 0x004014f1; called from `handleevents`, the per-frame event pump, at VA
  0x0049503d) drains `ManyMouse_PollEvent` and adds relative-motion
  events into `s_dx` (VA 0x005d3048, int) and `s_dy` (VA 0x005d3044, int). `dnGetMouseInput
  (int* dx, int* dy, int keep)` (body VA 0x00407db0, **thunk VA 0x00403c10 `E9 9B 41 00
  00`**, RVA 0x00003C10) returns them when `s_mode == 0` (VA 0x005d3050), clears them when
  `keep == 0`, and warps the OS cursor back to the parking spot (`s_mouseParkingX/Y`, VA
  0x005d304c/+4) through `SDL_WarpMouseInWindow`.
- **SDL fallback, used when `s_numDevices == 0`.** `readmousexy(long* x, long* y)` (body VA
  0x00493c40, **thunk VA 0x0040364d `E9 EE 05 09 00`**, RVA 0x0000364D) returns and clears
  the SDL layer's `mousex`/`mousey` (rva::RVA_MOUSEX/Y) unless the mouse is not acquired,
  the app is inactive or `moustat` is 0, in which case it returns 0.

`dnGetNumberOfMouseInputDevices()` (body VA 0x00407f70, thunk VA 0x00402865 `E9 06 57 00
00`) returns `s_numDevices`.

## B. Counts to the tic input (the dead zone and the axis mismatch)

`getinput(short snum)` (body VA 0x004f9080) runs once per tic and ends with
`dnOverrideInput(&loc)` (body VA 0x00495f00, **thunk VA 0x00401c5d `E9 9E 42 09 00`**, RVA
0x00001C5D), which owns the mouse entirely (the older `CONTROL_GetMouseDelta` at VA
0x0040a320 has no caller but its own thunk):

```
counts:  dnGetNumberOfMouseInputDevices() > 0 ? dnGetMouseInput(&dx, &dy, 0) : readmousexy(&dx, &dy)
yaw:     (xmousescale / 10) * dx * sensitivity.x
pitch:   aim_mode == 0 ? none (horz := 1) : (ymousescale / 10) * (xdim / ydim) * dy * sensitivity.y
gamepad: added to both from SDL_GameControllerGetAxis with a 0.27 dead zone (untouched by the mouse aim)
clamp:   +-max_velocity.x / .y (doubles at VA 0x005b36d8/+8, 1000.0 each)
apply:   loc.avel = clampi((int)yaw   + loc.avel, -127, 127)    <- _ftol2_sse TRUNCATES toward zero
         loc.horz = clampi((int)pitch + loc.horz, -127, 127)    <- same
         if (ud.mouseflip == 0) loc.horz = -loc.horz
```

- `sensitivity` is a pair of doubles at VA 0x005b36c8 (x) and 0x005b36d0 (y), static
  10.0 and 13.0, written by `dnSetMouseSensitivity` (VA 0x00496320) from the game's
  options. `xmousescale`/`ymousescale` are the cfg's MouseScaleX/Y (1..20, default 10).
- **The dead zone** is the truncation: any tic whose product is below 1.0 in magnitude
  contributes nothing, and nothing is carried to the next tic.
- **The axis mismatch** is the pitch's `(xdim / ydim)` factor (2.39 on 3440x1440), the
  13-vs-10 default, and the different unit sizes below.
- `loc` (rva::RVA_LOC, the `input` struct, 12 bytes): `avel` +0 (signed char), `horz` +1
  (signed char), `fvel` +2 (short), `svel` +4 (short), `bits` +8 (ulong). `loc` is the
  INPUT struct the tic consumes as `sync[snum]`; it is not player state.

## C. The tic's application (`processinput`, body VA 0x004fa450)

- Yaw: `if (avel != 0) { angvel = (avel << 1) * ksgn(...); ang = (ang + angvel) & 0x7ff; }`
  A 7/8 variant applies in one movement state (`local_28 == 2`, not identified): in that
  state the engine applies 7/8 of the step and the view leads by the remaining 1/8 for that
  tic, a per-tic sawtooth proportional to turn rate; accepted. **One `avel` unit is two angle
  units** = 2 / 2048 of a turn = 0.35 degrees, about 13 px at 3440 wide and 90 degrees.
- Pitch, `aim_mode != 0`: `horiz += horz >> 1`, then clamp -99..299. **One horizon unit
  needs `horz` = 2**; an odd `horz` loses its half. One horizon unit is about 0.45 degrees
  (about 17 px). `aim_mode == 0`: the game re-centres (`return_to_center`) and snaps
  `horiz` to 100 inside 96..104.
- Keyboard turning and looking add to the same fields through the same path
  (`angvel`/`horz` from `CONTROL_ButtonState`), which is why the mouse aim ADDS its yaw and
  pitch to `loc` after the original `getinput` rather than replacing `loc`.
- `look_ang` (+660) and `rotscrnang` (+810) are separate view terms `displayrooms` adds;
  they are unaffected.

## D. The renderer's view doubles (the per-frame precision)

`polymost_drawrooms` (body VA 0x0047a2d0) derives its view from the integer `globalang`
and `globalhoriz` (already interpolated by `displayrooms` between `oang`/`ang` and
`ohoriz`/`horiz` by `smoothratio`) and writes, in this order and all BEFORE the
`polymost_scansector` call at VA 0x0047AEE9 that the Disable culling option
([freecam-accuracy.md](freecam-accuracy.md) section G) also redirects (rva 0x0007AEE9, `E8 A2 64 00 00`):

| Global | RVA | Write site | Formula (project.cpp reproduces it exactly) |
|---|---|---|---|
| `gcosang` | 0x01774350 | 0x0047a51f | cosglobalang / 262144 = cos(ang * pi / 1024) / 16 |
| `gsinang` | 0x017732D0 | 0x0047a531 | singlobalang / 262144 = sin(ang * pi / 1024) / 16 |
| `gcosang2` | 0x01773280 | 0x0047a549 | viewingrange * gcosang / 65536 |
| `gsinang2` | 0x01774340 | after 0x0047a549 | viewingrange * gsinang / 65536 |
| `gshang` | 0x01773290 | 0x0047a5fa | dh / sqrt(dh^2 + ghalfx^2), dh = (ydimen >> 1) - horiz in pixels |
| `gchang` | 0x01773288 | 0x0047a61e | ghalfx / the same root |
| `ghoriz` | 0x00A14C60 | 0x0047a638 | ydimen / 2 (the horizon's pixel row) |
| `cosglobalang` | VA 0x01cd1688 (RVA 0x018D1688) | 0x004297e0 | 16384 * cos(ang * pi / 1024), long |
| `singlobalang` | VA 0x01c93f24 (RVA 0x01893F24) | 0x004297fa | 16384 * sin(ang * pi / 1024), long |
| `globalang` | VA 0x01dd4b5e (RVA 0x019D4B5E) | 0x00429683, in `drawrooms` from its angle argument | the integer view yaw itself, short |

`engine_gproj()` (project.cpp) reads them and `proj_gproj_recomputed(const CamState&, const short* sintable)` rebuilds them
from a `CamState` (angle, horizon, viewingrange, screen); the ESP's "gproj eng" and "gproj our" diagnostic lines match
for every frame logged. A hook at the scansector call site may overwrite the
seven doubles from a FRACTIONAL angle and horizon before any wall or sprite is drawn.
`globalhoriz` stays as it was (culling and the tic-clock view use it at integer precision);
`globalang` is rounded to the nearest unit for the parallax sky (below). Between tics the
drawn view leads the engine's sprite facing and culling by up to one tic of travel;
cosmetic. The mirror pass calls `polymost_drawrooms` again through the same site with the
mirrored `globalang`, so the override recomputes per call and applies its lead there too,
reflected (below). The doubles are recomputed by the engine every call: nothing to restore.

**The integer yaw pair.** `cosglobalang`/`singlobalang` (table above) is the engine's OTHER
copy of the view yaw, 16384-scaled longs read as `sintable[(globalang + 512) & 2047]` and
`sintable[globalang & 2047]`. The only writer of either is `drawrooms`, at VA
0x004297e0 (cos) and 0x004297fa (sin); `drawrooms` then calls `polymost_drawrooms` through its
thunk at VA 0x00429911 (thunk 0x004023ba, body 0x0047a2d0), so the pair is rewritten from the
integer `globalang` on every call, the mirror pass included (the mirror pass IS a second
`drawrooms` call from `displayrooms`, not a direct re-entry into `polymost_drawrooms`, so it
rewrites the pair from the mirrored `globalang` before that call's `polymost_drawrooms` body
runs). That is why the override writes the pair without restoring it: the next call, mirror or
not, starts by overwriting both longs again. Readers after our hook, all in the same
`polymost_drawrooms` call: `polymost_scansector` (0x004816ec, 0x00481734), `polymost_drawalls`
(0x0047b6f1, 0x0047b7de, 0x0047b807, 0x0047d16b, 0x0047dac2, 0x0047dcf4, 0x0047dde1, 0x0047de0a,
0x0047f6e7, 0x0048000a) and, in the masks pass that follows, `polymost_drawsprite` (0x00484100,
0x00484117); the software renderer's readers (wallmost, scansector, ceilscan, florscan,
grouscan, drawsprite, drawmasks) never run under Polymost. `polymost_drawrooms` itself reads
the pair only BEFORE the scansector call (0x0047a424, 0x0047a4b5, 0x0047a513, 0x0047ac06,
0x0047ac34), to build the six doubles above, so those reads see the integer values and our
override replaces both the doubles and the pair afterwards.

**Yaw and pitch focal lengths differ when the FOV is not 90.** Yaw's lateral
scale is halfx (`ghalfx`) * 65536 / viewingrange pixels per radian at the screen centre (993
px at FOV 120, `viewingrange` 113512, on a 3440-wide screen), while Build's shear
pitch, built from `gshang`/`gchang` above, always projects at that same halfx pixels per
radian regardless of `viewingrange` (1720 px on that same screen). At FOV 120 one degree of
pitch therefore moves the view 1.7 times as many pixels as one degree of yaw; the two agree
only at FOV 90 (`viewingrange` 65536), where the ratio is exactly 1.0. The menu's pitch match
(`s_pitchMatchLastFrame`) compensates.

**The parallax sky pans by the integer `globalang`.** The third copy of the view yaw is the
short `globalang` itself (table above). `polymost_drawalls` reads
it at six sites, all after our hook: 0x0047c951, 0x0047c99f
and 0x0047ca2c for the ceiling parallax, 0x0047eec1, 0x0047ef11 and 0x0047ef9e for the floor.
Each triple is the same arithmetic: tile index `(k + globalang) >> (11 - pskybits)`,
remainder `globalang - (index << (11 - pskybits))`, that remainder FILDed, FMULed by the
frame's pixels-per-unit scale and FADDed to `ghalfx` (VA 0x00e14c58) to give the screen x of
the sky seam. The seam's position is therefore a linear function of the INTEGER `globalang`.
`drawrooms` is the only writer (VA 0x00429683, from its own angle argument at the start of
every call) and sets it to the engine's interpolated integer angle, which carries none of this
frame's lead (applied * (1 - smoothratio) + carry + pending); without the override the walls
turn with the fractional view and the sky with the engine's integer one, differing by a lead
that changes every frame and can reach one tic of travel. The other readers of `globalang` are `drawrooms`
itself at 0x004297c5 and 0x004297e6 (before it calls `polymost_drawrooms`, i.e. before our
hook), `polymost_dorotatesprite` at 0x004847e9 (the HUD-model path, `tspr.ang = <model offset>
+ globalang`; this game uses no models), and `parascan`, `drawsprite` and `drawvox`, which are
the software renderer and never run under Polymost. Nothing writes it between our hook and the
end of the frame. So the override writes it, rounded to the nearest unit
(`mouseaim_globalang_of_view`, mouseaim.h), with NO RESTORE for the same reason the long pair
needs none: `drawrooms` rewrites it from its argument at the start of every call, the mirror
pass included, and the override reads the angle it leads from (`engine_camera()`) at its top
before any write, so nothing compounds across frames. The store is TWO BYTES and must stay two:
`numwalls` sits two bytes before and `sprite[]` two bytes after (common.h's note on
`rva::NUMWALLS`; test_esp pins the three apart).

The DLL's own reader of `globalang` is `engine_camera()` (engine.cpp, `c.ang`, masked
`& 2047`). Its callers are exactly two, both on the render clock: `mouseaim_render_override`
itself and `esp_draw` (esp.cpp), which `overlay_frame` (overlay.cpp) calls from
`hooked_swap`, the SDL_GL_SwapWindow hook (dllmain.cpp), i.e. AFTER the whole 3D pass and
so after this override, in the same frame. Nothing on the TIC clock reads it. The ESP's
`cam.ang` therefore sees the rounded fractional view instead of the engine's integer one, and
`engine_gproj()` carries the override's own doubles, so the projection and `cam.ang` agree to
within half a unit.

Accepted residual: the sky still pans in whole angle units. One unit is about 3 px at
FOV 120 on a 3440-wide screen (993 px per radian * pi / 1024), so a sub-unit step in
the sky remains. That is the engine's own quantisation of `globalang` (a short); the sky is
drawn within half a unit of the walls at every angle. `globalhoriz` stays unwritten: the
VERTICAL sky uses `ghoriz`/`gshang`/`gchang`, which the override already writes.

**The mirror pass.** A visible mirror is drawn as a SECOND `drawrooms` call, before the main
one. `displayrooms` runs `preparemirror` (VA 0x00446300 area) first: it computes the
reflected camera position and the reflected angle `tang = (getangle(dx, dy) * 2 - ang) &
2047` (the mirror wall's own direction doubled minus the camera angle) and sets the byte
`inpreparemirror` (RVA 0x0032B4A6, `rva::INPREPAREMIRROR`) to 1; then it calls
`drawrooms(tposx, tposy, z, tang, horiz, mirrorsector + MAXSECTORS)`, and only afterwards the
main `drawrooms` with the player's own angle. Inside `polymost_drawrooms` that byte is read
twice: at 0x0047a6c0, BEFORE our hook, where the engine negates `gstang` for the pass, and
again at 0x0047aef1, AFTER the `polymost_scansector` call at 0x0047AEE9 our override runs
from, where the engine flips `grhalfxdown10x` (0x01b73300) against `grhalfxdown10`
(0x00e04c40), CLEARS the byte at 0x0047af0a and draws the first bunch with
`polymost_drawalls(0)`. So at our hook the byte is still 1 on the mirror pass and 0 on the
main one, and `engine_in_prepare_mirror()` is a valid test of WHICH PASS this call is.

The override must apply to the mirror pass too. If it did not, the main pass would draw the
mirror wall and everything around it at the fractional view while the reflection seen through
that wall was drawn at the integer `tang` with no lead; the two would differ by the frame's
yaw lead (up to one tic of mouse travel), and the reflection would slide inside its frame
while the mouse turned. The reflection needs the SAME lead, mirrored: for a player view angle
A = ang + lead the reflected view angle is 2 * wallang - A = tang - lead, i.e. the lead simply
changes sign. The override writes that view (`mouseaim_mirror_view(v, cam.ang)`, mouseaim.h,
pinned by test tb_0095), taken against the interpolated angle the engine handed this pass
rather than against the wall, because on the mirror pass `cam.ang` IS `tang`, and
2 * tang - (tang + lead) is tang - lead whatever the wall was. The PITCH is not mirrored
(`preparemirror` passes `horiz` straight through), so the horizon lead is unchanged, and the
POSITION is the engine's own reflected one, which the override never touches.

Every write the override makes is safe on both passes. The six view doubles, the long pair
`cosglobalang`/`singlobalang` and the short `globalang` are all recomputed by
`drawrooms`/`polymost_drawrooms` at the start of each of the two calls, so writing them on the
mirror pass needs no restore, exactly as on the main one. The clip polygon is rebuilt from
values READ on this pass (`gctang`/`gstang` (the engine already negated `gstang` at 0x0047a6c0
for the mirror), `ghalfx`, `ghoriz` and the four window corners), so it is the mirror pass's
own region, and `initmosts` is a pure re-initialisation either way (section D1).
`camera_last_smoothratio()` is captured once per `displayrooms` and deliberately not cleared
(camera.cpp), so both passes see the same smoothratio; the pending counts are read without
consuming, so both passes see the same pending. The diagnostic publications
(`s_diagPendingX/Y`, `s_diagSmooth`, `s_diagYawLead`, `s_diagFrustumN`) are made on the MAIN
pass only, so the throttled line keeps describing the player's own view; `s_pitchMatchLastFrame`
stays unconditional because `viewingrange` is the same on both passes. `nocull`'s own sweep
stays gated off on the mirror pass.

### D1. The screen clip polygon

Writing those doubles is not the whole view. Still inside `polymost_drawrooms` and still
BEFORE the `polymost_scansector` call the override runs from, the engine builds the frame's
SCREEN CLIP POLYGON and installs it:

- the four viewport corners, taken one pixel outside the viewport on each side
  (`-1` and `windowx2 - windowx1 + 3`, likewise in y);
- each rotated by the frame's TILT, `gctang` (rva 0x01773298) and `gstang` (rva 0x01774320):
  `tx = oy*gstang + ox*gctang`, `ty = oy*gctang - ox*gstang`, with `ox`/`oy` measured from
  `ghalfx`/`ghoriz`;
- then by the PITCH: `vy = ghalfx*gshang + ty*gchang`, `vz = ghalfx*gchang - ty*gshang`;
- the resulting ring clipped against the near plane `z > 1` (a corner in front is kept; an
  edge that crosses contributes the crossing point, so the polygon has four to eight points);
- projected with `r = ghalfx / z` and handed to `initmosts(px, py, n)`: thunk rva
  0x00002CC5, `E9 76 60 07 00`, body rva 0x00078D40, `__cdecl` with three arguments the
  caller cleans.

`initmosts` is a pure RE-INITIALISATION of the `vsp` list every wall, floor and ceiling is
then clipped against: it sets `vcnt = 1` and lays the polygon in, accumulating nothing, so
calling it a second time in the same frame simply replaces the region rather than adding to
it. That is what makes the override's rebuild safe.

The override must rebuild it. The polygon is built from the pitch the engine computed; an
override that changes `gshang`/`gchang` afterwards leaves the geometry drawn with one pitch
and clipped to a region built for another. Both halves show on screen: the parts of the view
outside the stale region are not drawn (black void above and below) and the wall texture setup
disagrees with the clip (textures that move and morph). So the override computes the polygon
from the doubles it is about to write, BEFORE writing them (a polygon of fewer than three
points is a frame the engine itself would draw nothing for, and the override then leaves the
frame entirely alone, which it could not do once the engine's own doubles were overwritten),
and calls `initmosts` again after writing them.

## E. Interaction with existing brackets

- The free camera substitutes `ang`/`horiz` and consumes the mouse itself
  (`input_freecam_feed`, SDL motion events with the warp-echo filter); while it is active
  the mouse aim must contribute nothing and its accumulator must be discarded.
- The panel's input lockout (`freeze_engage`) zeroes `mousex`/`mousey` and the mouse
  gates; the accumulator must be discarded on the same frames.
- The aim assist writes `ang`/`horiz` on the tic (tracked view); the mouse aim's `loc`
  contribution is applied by the game before that write, as the game's own mouse is today.
