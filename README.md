<div align="center">

# Duke Nukem 3D Megaton Mod Menu

**Outlines, aim assist, a free camera and an ultrawide fix, one key away.**

A mod menu for Duke Nukem 3D: Megaton Edition on Steam.

![Game](https://img.shields.io/badge/Game-Megaton%20Edition-FFB000?labelColor=0E0F13&style=flat)
![Platform](https://img.shields.io/badge/Platform-Windows-FFB000?labelColor=0E0F13&style=flat)
![Release](https://img.shields.io/github/v/release/Nokama0/duke3d-megaton-menu?label=Release&color=FFB000&labelColor=0E0F13&style=flat)
![Build](https://img.shields.io/github/actions/workflow/status/Nokama0/duke3d-megaton-menu/build.yml?branch=main&label=Build&labelColor=0E0F13&style=flat)
![License](https://img.shields.io/badge/License-Unlicense-FFB000?labelColor=0E0F13&style=flat)

</div>

---

A mod menu for **Duke Nukem 3D: Megaton Edition** (Steam), build `1.4.9 STABLE`. Press **Insert** in game to open it.

It loads as a `duke3dmod.dll` that a patched copy of the game's own `duke3d.exe` pulls in at start-up. The same patch
also carries an ultrawide fix. The menu reads the game's state from the running engine and draws with the game's own
OpenGL renderer.

## What it does

The menu has five tabs.

**Visuals**
- Enemy boxes, health bars, names and distance
- Markers for items and pickups, secret areas, explodables, breakables, and switches and locks (with the solved
  combination tinted on the switch)
- Secret volumes drawn as panels in the world, a route to the nearest secret you have not found, secret triggers
- Damage numbers; draw distance up to 256 m, or unlimited

**Combat**
- Aimbot: hold key, smoothing
- Silent aim and triggerbot
- Target choice, aim point, cone, range and lead for moving targets

**Weapons**
- Perfect accuracy for the pistol, chaingun, shotgun and expander
- Modern mouse aim: raw input, equal axes, no dead zone
- Fire rate and weapon switch speed sliders

**View**
- Custom field of view
- A free camera: flight speed, cutaway view of the whole map, follow teleporters, teleport marker, no fog,
  screenshot mode, fullbright

**Settings**
- Rebindable keys (menu, free camera, teleport, aimbot button), UI scale
- Autosave, animations, debug log

Cheat-style features start off. Hover a row for a short explanation, and right-click a slider to type a number. The
panel moves and resizes, and remembers where you left it.

## Get it

You need Duke Nukem 3D: Megaton Edition on Steam, build `1.4.9 STABLE`, with its stock `duke3d.exe` (SHA-256
`0782CF3803B847F55EC06C031EB6046C0F9EDD1B49B31DB30673254B99704D45`). The patcher refuses any other exe. Python 3 is
needed once, to make the patched exe.

1. **Download** `duke3d-megaton-menu-<version>.zip` from the [latest release](https://github.com/Nokama0/duke3d-megaton-menu/releases/latest)
   and extract it.
2. **Rename** `duke3d.exe` to `duke3d.exe.orig` in `<game folder>\bin`. Keep it: it is your stock exe.
3. **Patch.** Make the patched exe from it (paths are yours to adjust):

   ```powershell
   python ultrawide_patch.py "<game folder>\bin\duke3d.exe.orig" "<game folder>\bin\duke3d.exe"
   ```

   The patcher checks the stock hash first. It refuses to write over its input or over a stock exe, and replaces an
   existing output only with `--force`, which keeps the old file as `duke3d.exe.bak`.
4. **Copy** `bin\duke3dmod.dll` from the zip into `<game folder>\bin`, next to `duke3d.exe`.
5. **Start** the game and press **Insert**.

To remove the menu, delete `duke3dmod.dll` from `<game folder>\bin`. The patched exe then runs the game stock, with
the ultrawide fix. To undo the patch too, copy `duke3d.exe.orig` back over `duke3d.exe`;
run `tools\uninstall.ps1` (or delete the DLL) before you do.

## Good to know

- **Autosave writes to save slot 9.** With Settings > Autosave above Off, the menu saves the game every N minutes
  into slot 9, the last of the ten, and **overwrites whatever save is in that slot**. It is off until you set it, and
  `autosaveslot` in `duke3dmod.ini` picks another slot (0 to 9). It saves only while you are alive and in play, not
  paused, in a menu or flying the free camera.
- **Where files go.** Settings are in `duke3dmod.ini` and the log in `duke3dmod.log`, both in `<game folder>\bin`. The
  menu never touches the game's own config.
- **Modern mouse aim replaces the game's mouse handling** while it is on, so the game's sensitivity setting has no
  effect.

---

## Build it

You need Windows with Visual Studio 2022 (the MSVC x86 32-bit toolset) and a Windows 10 SDK. The scripts find them
with `vswhere`.

```powershell
powershell -File src/duke3dmod/build.ps1
powershell -ExecutionPolicy Bypass -File tools\install.ps1 -GameDir "<game folder>"
```

### Tests

`build.ps1` runs the unit tests first, then compiles `build\duke3dmod.dll`. A red test fails the build; a good run
prints a line ending in `N passed, 0 failed`.

### Install

`install.ps1` copies the DLL into `<game folder>\bin`. It refuses while the game is running, and unless `bin\duke3d.exe`
is the patched exe (run `tools/ultrawide_patch.py` first, as above). It never writes `duke3dmod.ini`.
`tools\uninstall.ps1 -GameDir "<game folder>"` removes the DLL and leaves the ini, the log and the exe.

## Other menus

The same menu, ported to other Duke Nukem games.

| Game | Repository |
|---|---|
| Duke Nukem Forever | [dukenukem-forever-menu](https://github.com/Nokama0/dukenukem-forever-menu) |
| Duke Nukem: Manhattan Project | [dukenukem-manhattanproject-menu](https://github.com/Nokama0/dukenukem-manhattanproject-menu) |
| Alien Armageddon on EDuke32 | [eduke32-aa-menu](https://github.com/Nokama0/eduke32-aa-menu) |
| Duke Nukem 3D on EDuke32 with the High Resolution Pack | [eduke32-hrp-menu](https://github.com/Nokama0/eduke32-hrp-menu) |
| Duke Nukem 3D: 20th Anniversary World Tour | [duke3d-worldtour-menu](https://github.com/Nokama0/duke3d-worldtour-menu) |

## License

[The Unlicense](LICENSE): public domain, no warranty.
