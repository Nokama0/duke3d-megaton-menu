# Research notes

Engine facts behind the Nuke Panel menu for Duke Nukem 3D: Megaton Edition, read from
`duke3d.exe` (with its PDB) in Ghidra. Each note states what is known, with addresses,
offsets and field layouts, and ends with its open questions. VA = RVA + 0x400000.

Notes in `re/`:

| Note | Covers |
|---|---|
| [esp-sprites.md](re/esp-sprites.md) | Enemy discrimination, sprite iteration and data, the projection, max HP for health bars, the FOV slider. |
| [targeting.md](re/targeting.md) | Aimbot and silent aim: the two-clock rule, `cansee`, the engine's `aim`, `shoot`, the `atwith` values and projectile speeds, target liveness and velocity, the `horiz` clamp. |
| [aim-ahead.md](re/aim-ahead.md) | Why the aimbot missed while the player moved: the order inside `processinput` and the lead that fixes it. |
| [triggerbot.md](re/triggerbot.md) | The held fire bit, `shoot`'s hitscan ray and origin, the input record, the tic rate. |
| [breakable-obstacles.md](re/breakable-obstacles.md) | Which walls and sprites a player hitscan opens (`checkhitwall`, `checkhitsprite`), so the triggerbot shoots through them. |
| [freecam-accuracy.md](re/freecam-accuracy.md) | The freecam's `displayrooms` detour, sector tracking, perfect-accuracy spread sites, input lockout, `hitscan`, unrestrained camera. |
| [mouse.md](re/mouse.md) | The mouse path from raw counts to the tic input and the renderer's view doubles. |
| [fog-shade-hud.md](re/fog-shade-hud.md) | How Polymost lights a frame, the fullbright levers, distance fog, HUD and weapon drawing. |
| [damage-triggers-weapons.md](re/damage-triggers-weapons.md) | `ifhitbyweapon`, hidden triggers, weapon frames on `kickback_pic`, the panel's drawing surface. |
| [switches-breakables.md](re/switches-breakables.md) | `checkhitswitch` locks and combinations, explodable cracks, breakable walls, dying-enemy action records, `walltype`/`sectortype`/`player_struct`/`hittype` layouts. |
| [polymost-depth.md](re/polymost-depth.md) | How Polymost emits a vertex and its depth resolution, matrices and depth state, sprite corners, the weapon switch counter. |
| [msvc-bool-fold.md](re/msvc-bool-fold.md) | A compiler defect: MSVC 19.50.35728 x86 /O2 loses a `bool` member when constant-folding inline calls that return a struct. |
