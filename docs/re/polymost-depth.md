# Polymost's depth space, sprite corners and the weapon switch counter: engine facts

How Polymost emits a vertex and what depth value that gives, the matrices and depth state through
a frame, the corners of face and wall sprites, and the weapon switch counter. The menu's in-world
layer (switch tints and panels) is built on these.
Method: Ghidra (the Ghidra project's `/duke3d.exe`, PDB names). VA = RVA + 0x400000. Constants
were read from the data bytes at the addresses quoted, not assumed from source.

## A. How Polymost emits a vertex (drawpoly, body VA 0x00475190)

Both `bglVertex3d` sites in `drawpoly` (0x00476acd, 0x00476cfb) emit, for a vertex with screen
position `(px, py)` and per-vertex depth term `dd`:

```
r = 1.0 / dd
X = (px - ghalfx) * r * grhalfxdown10x
Y = (ghoriz - py) * r * grhalfxdown10
Z = r * (1.0 / 1024.0)            ; the double at VA 0x0059a1d8 is 0x3F50000000000000 = 2^-10
```

with the texture coordinates scaled by the same `r`. `dd` is the plane term `gdx * px + gdy *
py + gdo` the caller set up. For a face sprite `polymost_drawsprite` sets `gdx = gdy = 0` and
`gdo = gviewxrange / depth`, where `depth` is `dy * gsinang2 + dx * gcosang2` (the exact
quantity `project_world` returns as `ScreenPt::depth` and `ProjectDebug::gdepth`). So for any
world point whose screen position and `gdepth` the DLL already computes:

```
r = gdepth / gviewxrange
```

Sprites are depth-tested against walls by the game itself, so a quad emitted with the sprite
formula sits in the same depth space as everything Polymost drew.

### A2. The pitch rotation, and what `(px, py)` actually is

The `(px, py)` in the formula above is the PITCHED screen point, and `drawpoly` is what pitches
it. Its input is the UNPITCHED point `drawwalls`/`drawsprite` hand it; it then computes, at
`0x00475471`-`0x00475550`:

```
ox  = px - ghalfx                            ; 0x00475471
oy  = py - ghoriz                            ; 0x00475483
oz  = ghalfx                                 ; 0x00475492
oy2 = oy * gchang - oz * gshang               ; 0x004754a7-0x004754c1   (pitch)
oz2 = oy * gshang + oz * gchang               ; 0x004754c4-0x004754de   (pitch)
ox3 = ox  * gctang - oy2 * gstang             ; 0x004754e4-0x004754fb   (tilt; identity in play)
oy3 = ox  * gstang + oy2 * gctang             ; 0x004754fe-0x00475515   (tilt; identity in play)
                                              ; refuses oz2 < 1 at 0x00475527-0x00475534
r   = ghalfx / oz2                            ; 0x00475544-0x00475550
dd  = (px * gdx + py * gdy + gdo) * r         ; the plane term, SCALED by r
px' = ox3 * r + ghalfx                        ; the pitched screen point
py' = oy3 * r + ghoriz
```

and only then emits the vertex of section A from `(px', py')` and `dd`. Substituting gives, in
world terms:

```
X = ox3 * grhalfxdown10 / gdo
Y = -oy3 * grhalfxdown10 / gdo
Z = oz2 / (ghalfx * 1024 * gdo)          with 1 / gdo = gdepth / gviewxrange for a sprite
```

So the engine's Z carries `oz2` and the plane term `gdo` does not. `project_world` reproduces the
same rotation and the same `ghalfx / oz2` divide (project.cpp), so the screen point it returns is
already `(px', py')` and its `ProjectDebug::oz2` is the engine's own `oz2` for that point.

**Therefore**: a vertex built from that pitched screen point and from `r = gdepth / gviewxrange`
alone is the engine's vertex times `ghalfx / oz2` -- a UNIFORM scale on all three components, so
the screen position `X / Z` and `Y / Z` stays right and only the DEPTH is wrong. Level,
`oz2 == ghalfx` and the factor is 1 (the tint sits correctly on a centred view);
looking up or down moves `oz2` away from `ghalfx` and the quad lands behind or in front of the
surface it is painted on, so the depth test hides a different part of it and the tint appears to
slide with the pitch. `world_vertex` (world.h) multiplies the missing `oz2 / ghalfx` back in and
refuses `oz2 <= 0`, the plane `project_world` itself refuses on.

| Global (double) | VA | Use |
|---|---|---|
| `gshang` | written by `polymost_drawrooms` @0x0047a5fa | `dh / sqrt(ghalfx^2 + dh^2)`, the pitch sine |
| `gchang` | written by `polymost_drawrooms` @0x0047a61e | `sqrt(1 - gshang^2)`, the pitch cosine |
| `gstang` / `gctang` | from `gtang` @0x00e14c70, `setrollangle` @0x0044a300 | the screen roll; 0 and 1 in upright play |

| Global (double) | VA | RVA | Use |
|---|---|---|---|
| `ghalfx` | 0x00e14c58 | 0x00A14C58 (`rva::GHALFX`) | screen centre x |
| `ghoriz` | 0x00e14c60 | 0x00A14C60 (`rva::GHORIZ`) | screen centre y (`ydimen >> 1` after polymost_drawrooms' last write) |
| `grhalfxdown10` | 0x00e04c40 | 0x00A04C40 | `1 / (ghalfx * 1024)` |
| `grhalfxdown10x` | 0x01b73300 | 0x01773300 | the same, negated on the mirror pass |
| `gviewxrange` | 0x00e04c48 | 0x00A04C48 | the depth scale a face sprite divides by |
| `gxyaspect` | 0x01b74338 | 0x01774338 | the wall plane's scale (not used by the DLL: the sprite formula is) |

All are rewritten by `polymost_drawrooms` every call, mirror pass included; the DLL reads them
at the `polymost_scansector` call site on the MAIN pass (`engine_in_prepare_mirror()` false),
which runs after the mirror pass, so the captured values are the frame's final ones, with one
exception: `grhalfxdown10x` is assigned AFTER the scansector call (0x0047af04 negates it on the
mirror pass, the else branch copies `grhalfxdown10` on the main pass), so at capture time it
still holds the previous pass's sign. The DLL uses `grhalfxdown10` for X, which is what the main
pass assigns to `grhalfxdown10x` anyway.

### A3. The depth space itself: how fine a Build unit is

The question: how large is a world-space error, measured in the depth buffer, at the range a
player stands to press a switch (z-fighting of tints close to buttons).

`resizeglcheck` (body VA 0x00474f50, called from the top of `polymost_drawrooms` 0x0047a316)
loads, column-major, `m[0] = ydimen`, `m[2] = 1`, `m[5] = xdimen`, `m[6] = 1`, `m[10] = 1`,
`m[11] = ydimen`, `m[14] = -1`, and the modelview is identity. Written out, a vertex `(X, Y, Z)`
with `W = 1` gives

```
x_clip = ydimen * X
y_clip = xdimen * Y
z_clip = X + Y + Z - 1
w_clip = ydimen * Z
```

so `x_ndc = X / Z` and `z_ndc = (X + Y + Z - 1) / (ydimen * Z)`. The first is the check that the
vertex formula of section A is being read correctly: substituting it gives
`x_ndc = (px - ghalfx) * grhalfxdown10 * 1024 = (px - ghalfx) / ghalfx`, which is exactly a
viewport of width `xdimen == 2 * ghalfx`, as project.cpp's own note already states.

**The depth resolution.** At the screen centre `X == Y == 0`, so `z_ndc = (Z - 1) / (ydimen * Z)`
and, through a depth range of unit width, one 24-bit depth unit is `dZ = 2 * ydimen * Z^2 / 2^24`.
Section A's `Z = r / 1024` with `r = gdepth / gviewxrange`, and `gdepth` is the Build distance
`d` over 16 at a neutral viewingrange (`gcosang` is `cos / 16`), so `Z = d / (16384 * gviewxrange)`
and

```
one 24-bit depth unit  =  2 * ydimen * d^2 / (16384 * gviewxrange * 2^24)   Build units
```

With `gviewxrange = viewingrange * xdimen / 2^25` that is `ydimen * d^2 / (16 * xdimen * 2^24)`,
i.e. at 1920x1080 and a neutral viewingrange:

| d (Build units) | one depth unit |
|---|---|
| 64 | 8.6e-6 |
| 256 | 1.4e-4 |
| 1024 | 2.2e-3 |
| 8192 (the draw-distance cap) | 0.14 |

The shape is what matters and it is exact: the resolution goes as `d^2`, so an error that is
below the buffer's notice at a room's length is tens of thousands of depth units at arm's
length. Half a Build unit of endpoint rounding at `d = 64` is about 58,000 depth units, and the
in-world layer's `glPolygonOffset(-1, -2)` is two.

**Caveats.** `gviewxrange`'s own assignment was NOT read from Ghidra: the formula above is
carried from the engine's source, and only the global and its use (section A) are established
here. A `Z = d / (2 * xdimen)` reading would treat `d` as a Build distance when the substitution
above makes it the g-space depth `gdepth`, which is `d / 16`; that reading gives figures 16x
coarser than this table. Nothing downstream turns on which is used (the finer figure only makes
every error below larger in depth units), and the numbers in world.h are these.

**The two depth scales, and why they are only nearly equal.** Walls and wall sprites take their
depth through `gyxscale * gxyaspect`, face sprites (and this DLL's in-world layer, which uses the
sprite formula) through `gviewxrange`. `polymost_drawrooms` assigns

```
gyxscale    = xdimenscale / 131072                         ; @0x0047a4dd, read in project.cpp
gxyaspect   = xyaspect * viewingrange * 5 / 2^34
gviewxrange = viewingrange * xdimen / 2^25
```

(only the first has its write address established; the other two are carried from source, as above).
These are equal as real numbers, and unequal as computed ones, because `xdimenscale` is
`scale(xdimen, yxaspect, 320)` and `xyaspect` is `divscale32(1, yxaspect)` -- two integer
roundings, which put the two scales about 1e-5 apart. That is far below anything visible in
POSITION and, by the table above, thousands of depth units at close range.

**The float residual.** The engine computes its own endpoints in float32: `(float)(x - globalposx)`
has an ulp of 0.0078 at map coordinates near 65536, so the endpoints Polymost actually drew sit
up to about 0.01 Build units from the exact ones this DLL now computes in double -- about 1,200
depth units at `d = 64`. This is a residual nothing on our side can remove: it is the engine's
arithmetic, not ours.

**The bias.** Neither residual is a fixed screen-space quantity, so `glPolygonOffset` cannot
cover them; both are proportional to depth, and a proportional bias can. `world_vertex` (world.h)
scales all three vertex components by `1 - WORLD_DEPTH_BIAS` with `WORLD_DEPTH_BIAS = 1/512`,
which is a move along the view ray -- `X / Z` and `Y / Z` are untouched, so the quad does not
move on screen -- of `d / 512` Build units: 0.125 at `d = 64`, 2 at `d = 1024`, 16 at the 8192
cap. Against the table that is roughly 14,500 depth units at `d = 64` and 114 at the cap, beyond
both residuals at every range and invisible for a translucent panel at all of them. The engine
does the same thing in the only form GL's depth range allows: its 1e-5 range shift between the
3D pass and the masks pass (section B) is about 168 depth units, i.e. 0.0014 Build units at
`d = 64` -- which is LESS than the float residual, and is why the secret volume's panels, drawn
coplanar with walls from the other range, needed the bias too.

## B. Matrices and depth state through a frame

- The projection matrix is loaded by `resizeglcheck` (body VA 0x00474f50, thunk 0x00403224),
  called at the top of `polymost_drawrooms` (0x0047a316), only when the viewport changed; the
  modelview is identity. Neither is reloaded during the 3D pass, so `glGetFloatv` of
  `GL_PROJECTION_MATRIX` and `GL_MODELVIEW_MATRIX` at the scansector site returns exactly the
  matrices every wall and sprite of the frame is drawn with.
- 3D pass start, `polymost_drawrooms` 0x0047a31b-0x0047a350: `glEnable(GL_TEXTURE_2D)`,
  `glEnable(GL_DEPTH_TEST)`, `glDepthFunc(GL_ALWAYS)` (0x207), `glDepthRange(0.00001, 1.0)` (the
  double at 0x0059a248 is 0x3EE4F8B588E368F1 = 1e-5). Walls and floors are drawn front to back
  by the bunch sort with depth WRITES on and no test; the depth buffer is fully written.
- 3D pass end, 0x0047b124-0x0047b14c (rendmode 3 only): `glDepthFunc(GL_LEQUAL)` (0x203),
  `glDepthRange(0.0, 0.99999)` (the double at 0x0059a080 is 0x3FEFFFEB074A771D). The masks pass
  (sprites, masked walls) is drawn under these: tested against the walls, biased a hair toward
  the viewer by the range.
- `drawpoly` depth writes: `glDepthMask(1)` for an opaque polygon (0x0047692e, 0x00476bcb);
  for a translucent one it disables `GL_FOG`, enables `GL_BLEND` and sets `glDepthMask(0)`
  (0x0047697b, 0x00476c18). So opaque sprites write depth, translucent sprites and glass do not.
- `polymost_dorotatesprite` (the HUD, body 0x00484370): pushes and loads its own projection
  (0x00484ce0 `glPushMatrix`, 0x00484ced `glLoadMatrixf`), then `glDisable(GL_DEPTH_TEST)`
  (0x00484d09) and `glDisable(GL_ALPHA_TEST)`: the HUD neither tests nor writes depth. So at the
  time of the `displaymasks` hook (after the masks pass, before the HUD) the depth buffer holds
  the whole scene and the depth state is LEQUAL with range (0.0, 0.99999).

The DLL's in-world draw therefore: loads the captured matrices, sets `glDepthFunc(GL_LEQUAL)`
and `glDepthRange(0.0, 0.99999)` explicitly (never inherited), `glDepthMask(GL_FALSE)`, blending
on, and a polygon offset toward the viewer for quads coincident with a wall; everything inside
`glPushAttrib(GL_ALL_ATTRIB_BITS)` plus `glPushMatrix`/`glPopMatrix` on both stacks (attribute
pushes do not save matrices).

## C. Sprite corners (polymost_drawsprite, body VA 0x004829e0, decompiled)

`cstat >> 4 & 3` selects the kind. `xoff` = `picanm` x-offset byte plus `sprite.xoffset`,
negated when `cstat & 4`; `yoff` likewise from the `picanm` y-offset byte and `sprite.yoffset`,
negated when `cstat & 8`. `tilesizx/y` are the tile's pixel sizes.

- Face sprite (0): drawn as a screen-space box; `project_sprite_box` (project.h) already
  reproduces its corners from `proj_sprite_extents`. It has ONE plane term for all four corners
  -- `polymost_drawsprite` sets `gdx = gdy = 0` and `gdo = gviewxrange / depth` from the sprite
  centre, so the depth really is shared -- but it does NOT have one `oz2`: `drawpoly` rotates
  every corner separately (section A2), so the four vertices carry four different pitch terms,
  and across a pitched sprite filling half the screen those differ by percent of the depth. A
  world quad for it is therefore the four corners at that one depth, EACH with its own `oz2`
  (`project_sprite_corners`). Putting all four corners at that one depth with one `oz2` is
  wrong and z-fights. The remaining approximation is that the union box is a rectangle while the
  engine's pitched quad is not.
- Wall sprite (1): world endpoints, and they are FRACTIONAL -- `xv` is a sixteenth-of-a-texel
  quantity and the engine keeps `x0 = x - xv * f` a float all the way into `drawpoly`, so
  rounding the two ends to the integer lattice moves each by up to half a Build unit and tilts
  the drawn plane through the sprite's own. Invisible in position, about 58,000 24-bit depth
  units at `d = 64` (section A3). `project_world_d` (project.h) is the entry that
  takes them unrounded.
  `xv = xrepeat * sintable[ang & 2047] / 65536`, `yv = xrepeat * sintable[(ang + 1536) & 2047] / 65536`,
  `f = (tilesizx >> 1) + xoff`, `x0 = x - xv * f`, `y0 = y - yv * f`,
  `x1 = x0 + xv * tilesizx`, `y1 = y0 + yv * tilesizx`. Vertical extent: `z` is the sprite's z
  adjusted by `-4 * yrepeat * yoff`; if `cstat & 128` (centred) add `2 * tilesizy * yrepeat`
  (plus `2 * yrepeat` when `tilesizy` is odd); bottom = that z, top = z - `4 * yrepeat *
  tilesizy`. The top is clamped to the sector ceiling and the bottom to its floor when the
  respective parallax bit (`ceilingstat & 1`, `floorstat & 1`) is clear.
- Pickups use the face branch too (`polymost_drawsprite` VA 0x00482d70-0x00482dd8); its centring test
  (VA 0x00482ebe, `globalorientation & 0x80`) never consults `picnum`. `spawn` (VA 0x004c1e00) gives a
  pickup `xrepeat = yrepeat = 32`, except AMMO (picnum 40) at 16.
- Floor sprite (2): not a switch shape in this game; skipped.

A wall SWITCH (the ESP's markers with `index >= MAX_SPRITES` are wall textures, not sprites)
is the whole wall face: endpoints `wall[w].x/y` and `wall[point2].x/y`, top and bottom from the
owning sector's ceiling and floor at each endpoint through `getceilzofslope` /
`getflorzofslope` (bodies RVA 0x000465F0 / 0x00046750, `engine_florz`/`engine_ceilz` in
engine.cpp).

**The picanm offset bytes are NOT read.** Nothing in this DLL reads `picanm[]` at all, and no
RVA for it has been established, so the tint builds `xoff`/`yoff` from
`sprite.xoffset`/`sprite.yoffset` alone (with the same `cstat & 4` / `cstat & 8` negations the
engine applies). The residual error is that tile's picanm offset byte per axis -- zero for a
tile whose animation table entry carries no offset, which is the expected case for the switch
tiles. A wall-sprite switch whose tint sits a texel or so off its face is this, and the fix is
one `picanm` RVA plus two byte reads.

## D. The weapon switch counter (`processinput` decompile, lines 1083-1096; see damage-triggers-weapons.md)

`player_struct` (940 bytes, from the PDB layout): `weapon_pos` **+828** (short),
`last_weapon` **+698** (short), `holster_weapon` **+891** (char), `curr_weapon` +696,
`kickback_pic` +730.

Per tic, after the stomp branch: `if (weapon_pos != 0) { if (weapon_pos == -9) { if
(last_weapon < 0) { if (holster_weapon == 0) weapon_pos = 10; } else { weapon_pos = 10;
last_weapon = -1; } } else weapon_pos -= 1; }`. A weapon change starts the counter at -1 with
`last_weapon` set (the old weapon lowers over -1..-8, drawn by `displayweapon` from the
negative value), the flip at -9 starts the raise at 10 (counting 10..1 to 0). Holstering sets
`holster_weapon` and parks the counter at -9. The stomp branch (0x004fd8xx, `weapon_pos < 0 ->
weapon_pos = -weapon_pos`) only runs when `actorsqu` is set. Firing is refused while
`weapon_pos != 0` unless `holster_weapon == 1` (line 1112).

Event values for the menu's advance: the negative run may be advanced to at most -8 (the
engine's own step then reaches -9 and performs the flip), the positive run to at most 1 (the
engine's own step reaches 0), -9 and 0 are never written, and the counter is never moved from
0 or from -9.

## Open questions

- `gviewxrange`, `gxyaspect` assignments: formulas are from source, not re-read in Ghidra.
- The `picanm` array's RVA and offset bytes (switch tiles are expected to carry none).
