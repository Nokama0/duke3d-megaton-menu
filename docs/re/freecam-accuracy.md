# Freecam and perfect accuracy: engine facts

Read from Ghidra on the shipping `duke3d.exe` with its PDB (static). VA = RVA + 0x400000.
Confidence: read from the binary unless marked.

Summary: the freecam brackets `displayrooms` (render clock) and substitutes 8 pairs plus 3 singles of
player fields; the camera stays inside its sector's floor/ceiling span unless four clamp jumps are
patched; the teleport clamp uses `PHEIGHT = 0x2800`; perfect accuracy neutralises the `krand` calls in
`shoot`'s player arm; draw-all-sectors adds `polymost_scansector` calls under a counter budget.

See [targeting.md](targeting.md) for the player struct, `shoot`, the two-writer rule and the
`hook_jmp_thunk` pattern.

## A. `displayrooms` - the freecam detour point

```
void __cdecl displayrooms(short snum, long smoothratio)   // body VA 0x004c0ae0-0x004c18bd, thunk VA 0x0040182a
```
Thunk bytes at RVA 0x0000182A: `E9 B1 F2 0B 00` (JMP 0x004c0ae0). Hook with `hook_jmp_thunk`
exactly like `shoot`/`processinput`. It renders from `ps[snum]`; the game passes
`screenpeek` (`short`, VA 0x019f1c78, RVA 0x015F1C78, 115 xrefs) as `snum`.

**Substitution rule:** `displayrooms` interpolates position and view between the previous-tic and
current values with `smoothratio`. A camera substitute must write BOTH members of every pair it
touches, and restore all of them after the call: `posx/oposx` (+532/+568), `posy/oposy`
(+536/+572), `posz/oposz` (+540/+576), `ang/oang` (+652/+654), `horiz/ohoriz` (+544/+548),
`horizoff/ohorizoff` (+702/+552, set to 0/0 for the camera), `cursectnum` (+658), and
`pyoff/opyoff` (+580/+584, set to 0/0 so the camera does not bob). Leaving one member of a
pair produces an interpolation fight. Save before, write, call original, restore all: the
`SilentBracket` pattern from [targeting.md](targeting.md) (a pure, tested bracket over a
caller-supplied buffer; `silent.cpp` is the model).

This runs on the RENDER clock (per frame), which is correct for a camera: it does not
touch the tic-clock player update, and the restore happens before the tic runs again.
Nothing else reads the substituted fields during the call except the renderer, and the
ESP projects from `engine_gproj()`, which `displayrooms` derives from the substituted
values, so ESP markers follow the free camera.

## B. Sector tracking and the teleport clamp

```
void __cdecl updatesector (long x, long y,          short* sectnum)   // body VA 0x004437f0, thunk 0x0040214e
void __cdecl updatesectorz(long x, long y, long z,  short* sectnum)   // body VA 0x00443910, thunk 0x00401d20
long __cdecl getflorzofslope(short sectnum, long x, long y)          // body VA 0x00446750, thunk 0x004015fa
long __cdecl getceilzofslope(short sectnum, long x, long y)          // body VA 0x004465f0, thunk 0x00403e18
```
Call the bodies by RVA the way `engine_cansee` calls `cansee` (cdecl, bounds-checked
sector in, `sectnum` in/out is a `short*`; pass the current sector as the starting guess).
`updatesector` returns -1 in `*sectnum` when the point is outside every sector, which
is the "camera in solid space" signal the teleport refuses on. Use
`updatesectorz` for the camera each frame (it disambiguates stacked sectors by z).
Teleport clamp: after `updatesectorz` succeeds, `floor = getflorzofslope(sect, x, y)`,
`ceil = getceilzofslope(sect, x, y)`; place the player's `posz` at
`min(max(cameraZ, ceil + PHEIGHT_MARGIN), floor - PHEIGHT)` with `PHEIGHT = 0x2800` and
`PHEIGHT_MARGIN = 0x400` (section E2). `0x2000` is NOT the standing eye height: it is `aim()`'s offset
from a SPRITE's z. Also write `oposx/y/z` = the new position and `cursectnum` = the
new sector, and reset `poszv` (+596) to 0 so the player does not carry a fall velocity.

## C. Perfect accuracy: the spread sites in `shoot`

`shoot`'s player arm decompiles to:
`if (atwith == 0xa23) { if (aim() == -1) { yaw krand; pitch krand; } } else { yaw krand;
if (aim() == -1) { ...set sa/zvel toward the target... }; pitch krand; }`.
The PISTOL's two krand calls sit INSIDE the `aim() == -1` guard and are skipped whenever
auto-aim has a target. The CHAINGUN's, SHOTGUN's and EXPANDER's yaw and pitch krand calls
are OUTSIDE that guard: they run unconditionally, whether or not auto-aim found something.
The guard only gates the middle block (deriving `sa`/`zvel` toward the found target) for those three.

Consequence: the "Pistol" toggle in the Weapons tab is a no-op on any shot where Duke's auto-aim has
already found a target (the pistol's spread never ran on that shot). "Chaingun", "Shotgun" and "Expander"
have no such exemption: their spread runs on every one of their `atwith` shots, so their toggles
always bite. The comment on `accuracy_pair_for` in accuracy.h carries this asymmetry too.

With silent aim, `auto_aim` is held at 0 during the shot regardless of weapon, so `aim()`
always returns -1 and EVERY weapon's spread runs; perfect accuracy is what makes
those shots land, for all four weapons alike.

Every site is a 5-byte `E8 rel32` call to the `krand` thunk (0x004018d9) immediately
followed by `AND EAX, mask` and `MOV reg, centre; SUB reg, EAX`, i.e. `centre - (krand & mask)`.
Replacing the 5-byte call with `B8 imm32` (`MOV EAX, imm`) with `imm == centre` yields a
spread of exactly 0 with no other side effect: the instruction is the same length, `EAX`
is the only register the call would have defined, and `krand`'s internal state is simply
not advanced for that call (the engine's RNG sequence shifts by one draw; harmless).

| Weapon (player `atwith`) | Yaw site (`&0x1f`, centre 16) | Pitch site (`&0xff`, centre 128) |
|---|---|---|
| Pistol, SHOTSPARK1 `0xa23` (only when `j == -1`) | VA 0x004f422c | VA 0x004f4273 |
| Chaingun `0x9e8` and Shotgun `0xa35` (shared branch) | VA 0x004f428c | VA 0x004f42dc |
| Expander, GROWSPARK `0x990` (own handler at 0x004f5ceb) | VA 0x004f5e00 | VA 0x004f5e47 |

Bytes at each site: `E8 xx xx xx xx` (rel32 to 0x004018d9; the test pins each
rel32 the way `THUNK_SHOOT_BYTES` is pinned). Patch: `B8 10 00 00 00` for yaw
sites, `B8 80 00 00 00` for pitch sites. The expander's ACTOR path (0x004f5ed5 `&0xff`,
0x004f5eec `&0x3f`) is enemy fire: never patch it. The shotgun fires 7 pellets via 7
`shoot(i, 0xa35)` calls ([targeting.md](targeting.md) section D), each through the shared branch, so "perfect"
means all 7 converge, which is why the menu defaults the shotgun toggle OFF. RPG,
devastator, freezer, shrinker have no spread sites (projectiles fly straight from `sa`/`zvel`).

**Mechanism (inside the detour, not by detouring krand):** the `shoot` thunk hook
(`hooked_shoot`) decides per shot from `atwith` and the per-weapon toggles which of the three
site pairs to neutralise; `VirtualProtect` the two sites writable, verify the ORIGINAL bytes are
the expected `E8` + rel32 (fingerprint; on mismatch do nothing and disable the feature), write the
`B8` form, call the original `shoot`, restore the original bytes, restore protection. One shot =
one bracket, on the same single thread as everything else; the sites are shared between chaingun
and shotgun but the decision is per `atwith`, so the toggles stay independent. The decision is a
pure tested function (`accuracy_sites_for(atwith, toggles)`) plus a pure byte-bracket over a
caller-supplied buffer (model: `SilentBracket`).

## D. Input lockout for the freecam

The game reads keys two ways, both hooked in `input.cpp`: `SDL_PollEvent`
(events) and `SDL_GetKeyboardState` (the array), plus the engine's `keystatus` and the
`loc` input record cleared on the tic clock in `hooked_getinput`. The panel-open freeze
implements "player input off, DLL input on"; the freecam reuses the SAME
mechanism (the `s_menuOpen`-style state that the freeze checks). Mouse
deltas for the camera come from the SDL `MOUSEMOTION` events `hooked_poll` already sees
(xrel/yrel); WASD/Shift/Ctrl from `SDL_KEYDOWN/UP`.

## E. What else `displayrooms` reads, and `PHEIGHT`

### E1. Other fields `displayrooms` reads (decompiled, body VA 0x004c0ae0)

Beyond section A's set, the single-player `ps[]` branch reads these. The first
three CHANGE THE SUBSTITUTION SET; the rest are preconditions or context.

| Field | Offset | What `displayrooms` does with it |
|---|---|---|
| `look_ang` | +660 (short) | **Added to the interpolated yaw UNCONDITIONALLY** (`local_68 += ps->look_ang`, immediately after the yaw interpolation). A camera that leaves it alone carries the player's peek. **Zero it.** |
| `rotscrnang` / `orotscrnang` | +810 / +808 (shorts) | An interpolated PAIR like the others. On rendmode > 0 (this game): `setrollangle(orotscrnang + mulscale16(((rotscrnang - orotscrnang + 0x400) & 0x7ff) - 0x400, smoothratio))`, and then `displayrooms` **writes `orotscrnang = rotscrnang` itself**. Left alone, the camera inherits the damage roll. **Zero both.** |
| `newowner` | +708 (short) | If `>= 0`, the ENTIRE view is overwritten from `sprite[newowner]` (pos, ang, horiz, sector) and `smoothratio` is forced to 0x10000. **Precondition: must be < 0**, else the substitution is computed and discarded. |
| `over_shoulder_on` | +746 (short) | If non-zero, calls `view()`, which rewrites x/y/z/sector. **Precondition: must be 0.** |
| `cursectnum` | +658 | Gates the whole function (returns early at -1) and the render block (`0 <= cursectnum < 0x1000`). Already in A's set. |
| `spritebridge` +886, `truecz` +608, `truefz` +604 | | Gate a z clamp: `if (z < truecz + 0x400) z = hittype[ps->i].ceilingz + 0x400; else if (z > truefz - 0x400) z = hittype[ps->i].floorz - 0x400;` |
| (none) | | **An UNCONDITIONAL second z clamp** follows it: `if (cursectnum >= 0) { getzsofslope(cursectnum, x, y, &ceil, &flor); z = clamp(z, ceil + 0x400, flor - 0x400); }`. **The camera can never leave the floor/ceiling span of the sector it reports.** That is why `updatesectorz` tracking per frame (section B) is mandatory: a freecam that stops updating `cursectnum` gets clamped into the old sector's span. |
| `visibility` +616, `on_ground` +850, `on_crane` +740, `sprite[ps->i].pal`, `sprite[ps->i].yrepeat` | | Fog, earthquake shake, pause, underwater tint, aspect. Not view-substitution relevant. |

The `param_1 == myconnectindex && numplayers > 1` arm reads the
`my*` GLOBALS (`myx/omyx/myang/omyang/myhoriz/mycursectnum`) instead of `ps[]`
entirely. `numplayers` is 1 in this single-player game, so the `ps[]` branch in A
is the one a camera substitutes; a coop build would need the `my*` set too.

**Consequence for `CameraBracket`:** the field set is **8 pairs**
(posx/oposx, posy/oposy, posz/oposz, ang/oang, horiz/ohoriz, horizoff/ohorizoff,
pyoff/opyoff, **rotscrnang/orotscrnang**) plus **three singles** (cursectnum,
poszv, **look_ang**). `newowner` and `over_shoulder_on` are checked by the
caller, not substituted.

### E2. `PHEIGHT` is **0x2800**

`processinput` keeps the player's `posz` (which IS the eye) at `floorz - (i << 8)`
with `i = 40` when standing:

```
VA 0x004fb67a  MOV  dword ptr [EBP + -0x1c], 0x28     ; i = 40, the standing height
VA 0x004fbf80  SHL  ECX, 0x8                          ; i << 8 = 0x2800
VA 0x004fbf83  MOV  EDX, [EBP + -0x3c]                ; floorz
VA 0x004fbf86  SUB  EDX, ECX                          ; floorz - 0x2800
VA 0x004fbf8b  CMP  dword ptr [EAX + 0x21c], EDX      ; posz < that  -> falling
VA 0x004fbf91  JGE  0x004fc1ec                        ;              -> on ground

on the ground arm (VA 0x004fc287-0x004fc2ff):
VA 0x004fc287  CMP  dword ptr [EBP + -0x1c], 0x28     ; the standing case
VA 0x004fc290  SHL  EAX, 0x8                          ; 0x2800 again
VA 0x004fc296  SUB  ECX, EAX                          ; ECX = floorz - 0x2800
VA 0x004fc29b  SUB  ECX, [EDX + 0x21c]                ; ... - posz
VA 0x004fc2a1  SAR  ECX, 1                            ; half the gap
VA 0x004fc2aa  CALL klabs ; CMP EAX,0x100 ; JGE       ; snap when |gap/2| < 256
VA 0x004fc2cf  MOV  dword ptr [EAX + 0x21c], EDX      ; posz += that
VA 0x004fc2de  SUB  EDX, 0x300 ; ... poszv (+0x254), clamped at 0
```

So the RESTING eye height above the floor is exactly **`40 << 8 = 0x2800`
(10240)**. The crouched arm (VA 0x004fc316-0x004fc338) uses `SHL EDX, 0x7`, i.e.
`floorz - (40 << 7)` = `floorz - 0x1400`. The ceiling side keeps `posz` at
`ceilingz + 0x400` and zeroes `poszv` (VA 0x004fb8a5-0x004fb8cb), which is where
`PHEIGHT_MARGIN` comes from. Two other heights exist for other states:
`i = 0x22` (34) at VA 0x004fbc73 and `i = 0xc` (12) at VA 0x004fbccb.

**`0x2000` is NOT this number.** It is `aim()`'s offset from a *sprite's* `z`
(`sprite[t].z - 0x2000`), used as the LOS reference point for shooter and target
alike ([targeting.md](targeting.md) section A). It is not the player's standing eye height above the
sector floor. The teleport clamp uses `PHEIGHT = 0x2800` and `PHEIGHT_MARGIN = 0x400`.

## F. `hitscan` - the teleport's ray cast

`hitscan` body VA `0x0043f180` = RVA `0x0003F180` (thunk VA `0x00403d05`).
`cdecl`, 14 args, the caller adds `0x38` to ESP after the call. Signature
(Build's own):

```
long hitscan(long xs, long ys, long zs, short sectnum,
             long vx, long vy, long vz,
             short* hitsect, short* hitwall, short* hitsprite,
             long* hitx, long* hity, long* hitz,
             unsigned long cliptype)
```

Confirmed from the player's own shot call at VA `0x004f39bf`-`0x004f3a23`, which
pushes (last first) `cliptype = 0x01000040`, `&hitz`, `&hity`, `&hitx`,
`&hitsprite`, `&hitwall`, `&hitsect`, `vz`, `vy = sintable[ang & 2047]`,
`vx = sintable[(ang + 512) & 2047]`, `sect` (movzx word), `zs`, `ys`, `xs`.

**Direction for a view with `ang`, `horiz`** (no `horizoff` term for the
camera, since it has none):

```
vx = sintable[(ang + 512) & 2047]
vy = sintable[ang & 2047]
vz = ((100 - horiz) << 5) << 6
```

the same `vx`/`vy` convention `shoot`'s own hitscan call uses; the no-target
hitscan builds `zvel = (100 - horiz - horizoff) << 5` and passes it as
`zvel << 6` ([targeting.md](targeting.md) section C).

**`cliptype = 0x01000040`** is `CLIPMASK1` (blocking walls + hittable
sprites), the SAME mask the player's own shots use, so the teleport's ray
stops exactly where a shot from that pose would.

**Result:** `*hitsect < 0` means nothing was hit (refuse the teleport
outright); otherwise `(hitx, hity, hitz)` is the impact point and exactly one
of `*hitwall >= 0`, `*hitsprite >= 0`, or neither (a floor/ceiling hit) says
what was hit. `engine_hitscan` (engine.cpp) wraps the real body behind
`engine_verified()` and a sector bound on the STARTING `sectnum` only (the
same shape `engine_cansee` has), and `rva::HITSCAN_BODY_RVA` is the
named constant for the body address above.

**`ud.pause_on`:** Duke's own pause flag, a 16-bit value at VA `0x01b645b8` = RVA `0x017645B8`.
`domovethings` tests it with `MOVSX ECX, word ptr [0x01b645b8]` at VA
`0x004d18cc`, immediately before the `processinput` call it gates; nonzero
means paused. `processinput` (and with it `camera_tic`, the
teleport edge's only normal drain) is gated on this flag, so a freecam edge
latched while paused would bank until the game resumed. `hooked_getinput` runs every frame
regardless of the flag, so it is the one hook that can read it and discard the edge
before it banks. `rva::PAUSE_ON` (common.h) is the named constant;
`engine_paused()` (engine.cpp) is the bounds-checked accessor, gated behind
`engine_verified()` like every other raw read in that file.

## G. Unrestrained camera and draw-all-sectors

VA = RVA + 0x400000. All from the shipping `duke3d.exe` with its PDB (static).

**`displayrooms` (body VA `0x004c0ae0`) clamps the camera four times before
calling `drawrooms`.** Each clamp is a 2-byte conditional jump; making it
unconditional (`EB`, same length, same displacement byte) skips the clamp it
guards:

| Site | RVA | Expected | Replacement | What it skips |
|---|---|---|---|---|
| `CLAMP_BRIDGE` | `0x000C1455` | `75 40` (`JNZ 0x004c1497` on `spritebridge != 0`) | `EB 40` | the gated `truecz + 0x400` / `truefz - 0x400` clamp (only reached when `newowner < 0`, which the camera already requires) |
| `CLAMP_SECTOR` | `0x000C149D` | `7C 50` (`JL 0x004c14ef` on `cursectnum < 0`) | `EB 50` | the unconditional block `getzsofslope(cursectnum, x, y, &cz, &fz); z = clamp(z, cz + 0x400, fz - 0x400)` |
| `CLAMP_HORIZ_HI` | `0x000C14F8` | `7E 0B` (`JLE 0x004c1505` on `horiz <= 299`) | `EB 0B` | `horiz = 299` (falls through to the low check) |
| `CLAMP_HORIZ_LO` | `0x000C150C` | `7D 09` (`JGE 0x004c1517` on `horiz >= -99`) | `EB 09` | `horiz = -99` |

All four sites sit within VA `0x004c1455..0x004c150c`, i.e. one 4 KB page
(`0x004c1000`). After the clamps `displayrooms` calls
`se40code(x, y, z, ang, horiz, smoothratio)` then
`drawrooms(x, y, z, ang, (int)horiz, cursectnum)`. `horiz` there is a SHORT
local (`local_10`): values up to +-32767 are representable, and `drawrooms`
puts NO cap on it: `globalhoriz = mulscale16(horiz - 100, xdimenscale) +
(ydimen >> 1)`. Polymost derives the pitch as `r = (ydimen>>1) - ghoriz;
gshang = r / sqrt(r*r + ghalfx*ghalfx); gchang = sqrt(1 - gshang^2)`, which
saturates smoothly toward +-90 degrees as `|horiz|` grows. So
`tan(pitch) = (horiz - 100) / 128` holds for any
`horiz` once the four clamps are gone; the pitch representation
(`FREECAM_PITCH_MAX`, freecam.h) is built on this.

**Sector visibility.** `drawrooms` (VA `0x00429650`) clears the visited set
every frame, `memset(gotsector, 0, (numsectors + 7) >> 3)`, then calls
`polymost_drawrooms` (VA `0x0047a2d0`), which sets `numbunches = 0; numscans =
0;`, re-resolves `globalcursectnum` with `updatesector` (holding the previous
value when that fails), then calls `polymost_scansector((int)globalcursectnum)`
ONCE from the call site at VA `0x0047aee9` (`E8 A2 64 00 00`, cdecl, one arg,
`ADD ESP, 4` follows) and then sorts and draws the bunches with
`polymost_bunchfront`/`polymost_drawalls`.

`polymost_scansector` (body VA `0x00481390`, RVA `0x00081390`,
`void __cdecl (long sectnum)`) returns at once for `sectnum < 0`, otherwise
seeds `sectorborder[0] = sectnum`, marks
`gotsector[sectnum >> 3] |= 1 << (sectnum & 7)` UNCONDITIONALLY for the seed,
collects that sector's sprites into `tsprite`, and BFS-walks portals
(`nextsector` not yet in `gotsector`, wall not `cstat 0x20`, within the 260.0
distance test), APPENDING wall scans to `numscans` and bunches to
`numbunches` (it does not reset either count). It is a small BFS, not a
single-sector scan: `sectorborder[0] = sectnum;
sectorbordercnt = 1; do { pop; scan its whole wall loop, appending to both
counters; push each unmarked neighbour whose portal wall the camera is
within ~16 units of (`d*d <= len*len*260.0`, SCISDIST 1.0) } while
(0 < sectorbordercnt)`. ONE call can therefore append more than the popped
sector's own `wallnum`, which `NOCULL_CALL_ALLOWANCE` (below) covers.

**A SECOND call site, in `polymost_drawalls` (VA `0x0048125B`), runs AFTER
`polymost_drawrooms`' own bunch-sort/draw pass has started, guarded by its
own `gotsector` read at VA `0x00481201`.** It calls `polymost_scansector`
for every sector still NOT marked in `gotsector` at that point: in
vanilla play, the engine's own real portal-visibility walk continuing past
whatever the seed call reached; with the sweep installed, exactly the set of
sectors the sweep left unmarked when it stopped.
It charges the SAME two counters AFTER this DLL's hook has returned, so the
sweep's budget has to leave that second pass real room to run in. The DLL does not hook
or touch this second call site.

Globals: `gotsector` RVA `0x01890FE0` (`uchar[512]`, bit `s & 7` of byte
`s >> 3`), `numsectors` RVA `0x018907A0` (short), `globalcursectnum` VA
`0x01E861A6` (RVA `0x01A861A6`, short; `0x01E861A6` is the VA, read at VA
`0x0047AEE1`). Nothing in the DLL uses `globalcursectnum` as a constant.

**The engine does not bound `numscans`/`numbunches`; the sweep supplies the bound.**
`polymost_scansector` (body VA `0x00481390`) does `numscans = numscans + 1` and
`numbunches = numbunches + 1` on every call with no `CMP` against a limit
anywhere in the function; the only bound present,
`spritesortcnt < 0x1000` (VA `0x00481480`), protects `tsprite` and nothing
else. `polymost_drawrooms` (VA `0x0047A2D0`) zeroes both once per frame,
immediately before the seed call (`numbunches` at VA `0x0047AE68`, `numscans`
at VA `0x0047AE75`), and never bounds them again. Both are `short`s:
`numscans` VA `0x01ED8980` -> RVA `0x01AD8980`; `numbunches` VA `0x01DD2620` ->
RVA `0x019D2620`. The arrays they index (`thesector` (VA `0x01CCA840`),
`thewall` (VA `0x01D782A0`), `p2` (VA `0x01DCF620`), `bunchfirst`
(VA `0x01E8E760`), `bunchlast` (VA `0x01CC5400`, all `short[MAXWALLSB]`) and
`dxb1`/`dxb2` (VA `0x00E04C50`/`0x00DD0C40`, `double[MAXWALLSB]`)) are all
exactly `MAXWALLSB = 6144` entries. Vanilla play never exhausts them because
the single-seed portal BFS, the `cstat 0x20` wall test and the 260.0-unit
distance test bound how much of the map one frame's scan can reach; the
draw-all-sectors sweep removes that bound, so `nocull_budget_allows` (`nocull.h`)
re-reads both live counters and the next sector's own `wallnum` before every
extra call and stops the sweep once a budget would be exceeded.

**The reserve rule.** A flat 64-entry margin is too small: on a map big enough to exhaust
the budget, the sweep would fill `numscans`/`numbunches` to within 64 of `MAXWALLSB`, then
the SECOND `polymost_scansector` call site would scan every sector the sweep left unmarked
with only 64 entries of headroom, and a single sector's wall loop is routinely more than
that. The rule: at hook entry, after the seed call and before the sweep's own loop, compute
`free = MAXWALLSB - max(numscansEntry, numbunchesEntry)`; the sweep may
spend AT MOST `free / 2`, leaving the other half for
that second call site. Before every extra sector, re-read both
counters LIVE and take `usedSinceEntry = max(numscansNow, numbunchesNow) -
max(numscansEntry, numbunchesEntry)`; refuse once
`usedSinceEntry + wallnum + NOCULL_CALL_ALLOWANCE (256) > free / 2`. An
ABSOLUTE FLOOR runs first and independently: refuse outright
once either LIVE counter exceeds `MAXWALLSB - 1024`, whatever the
half-of-free-at-entry budget would still permit. All of this lives in
`nocull_budget_allows` (`nocull.h`), pure and pinned by the suite; `nocull.cpp`
decides nothing of it.

**`inprepamirror`: `displayrooms` runs `polymost_drawrooms` twice on a mirror frame.**
`displayrooms` renders a visible mirror by calling `preparemirror` and a second `drawrooms`
before the main one (the `gotpic[0x46] & 1` block in its own decompile), so
`polymost_drawrooms`, and the seed call the hook replaces, runs
TWICE on such a frame. The engine's own flag, `inprepamirror`, is one byte at
VA `0x0072B4A6` (RVA `0x0032B4A6`), read at VA `0x0047AEF1`, immediately
*after* the seed call, so it is already set while the hook body runs on the
mirror pass. `nocull_should_scan` (`nocull.h`) takes this as a third,
negated term so the sweep does not run twice on a mirror frame.

**Draw-all-sectors:** calling the body again for every sector not yet in `gotsector`,
right after the seed call, puts every sector's in-frustum walls into the bunch list; the
renderer's own front-to-back sort and `domost` occlusion then draw them normally. Cost: the
sort is O(bunches^2) per frame, so large maps are slower. The option therefore defaults off and
is diagnosed with a throttled log line rather than drawn as a performance readout.
