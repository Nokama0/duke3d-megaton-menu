param([switch]$RegenFont)   # -RegenFont rewrites font_data.cpp from two Windows fonts; needs python with Pillow on PATH
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = if (Test-Path $vswhere) {
  & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsroot) { throw "Visual Studio with the C++ x86/x64 build tools was not found (vswhere.exe)" }
& (Join-Path $vsroot "Common7\Tools\Launch-VsDevShell.ps1") -Arch x86 -HostArch amd64 -SkipAutomaticLocation

# font_data.cpp is generated but tracked: a normal build uses the committed copy.
if ($RegenFont) {
  if (Get-Command python -ErrorAction SilentlyContinue) { & python gen_font.py } else { & py -3 gen_font.py }
  if ($LASTEXITCODE -ne 0) { Write-Error "font generation failed"; exit 1 }
}

New-Item -ItemType Directory -Force ..\..\build | Out-Null

# DLL-only (never linked into test_host): they touch live engine memory, hooks or the OS (fontgen.cpp and dollhouse.cpp
# also bring gdi32.lib and glu32.lib). Every decision they make is a pure rule in a header the suite pins (target.h,
# nocull.h, mouseaim.h, dmgnum.h, trigger.h, firerate.h, world.h, esp_tint.h, wswitch.h, actorstate.h, entry.h, ...).
cl /nologo /O2 /W3 /EHsc /MT /MP /Fo..\..\build\ dllmain.cpp hooks.cpp input.cpp engine.cpp config.cpp log.cpp fov.cpp ui.cpp overlay.cpp canvas.cpp font_data.cpp fontgen.cpp menu.cpp project.cpp esp.cpp esp_names.cpp target.cpp aim.cpp silent.cpp freecam.cpp camera.cpp nocull.cpp shot.cpp mouseaim.cpp wallclock.cpp dmgnum.cpp trigger.cpp firerate.cpp world.cpp esp_tint.cpp wswitch.cpp tbot.cpp actorstate.cpp clipboard.cpp autosave.cpp dollhouse.cpp secretpath.cpp `
   /link /DLL /OUT:..\..\build\duke3dmod.dll user32.lib opengl32.lib gdi32.lib glu32.lib
if ($LASTEXITCODE -ne 0) { Write-Error "duke3dmod.dll build failed"; exit 1 }
Write-Host "duke3dmod.dll build OK"

# The suite is one translation unit per area (tools\split_tests.py made them). test_host.cpp is the ORDER of the tb_
# block functions; tests\ holds the blocks and shared prelude, listed explicitly (not by wildcard) so the DLL line
# above never picks one up.
cl /nologo /O2 /W3 /EHsc /MT /MP /DNO_GL /Fo..\..\build\ test_host.cpp tests\test_common.cpp tests\test_config.cpp tests\test_esp.cpp tests\test_freecam.cpp tests\test_light.cpp tests\test_menu.cpp tests\test_misc.cpp tests\test_platform.cpp tests\test_shot.cpp tests\test_target.cpp tests\test_mouseaim.cpp tests\test_phase8.cpp tests\test_phase9.cpp tests\test_tbot.cpp tests\test_actorstate.cpp tests\test_tooltip.cpp tests\test_panel.cpp tests\test_entry.cpp tests\test_autosave.cpp tests\test_dollhouse.cpp tests\test_secretpath.cpp log_testshim.cpp config.cpp fov.cpp ui.cpp canvas.cpp font_data.cpp menu.cpp menu_stubs.cpp project.cpp esp_names.cpp target.cpp freecam.cpp `
   /link /OUT:..\..\build\test_host.exe
if ($LASTEXITCODE -ne 0) { Write-Error "test_host build failed"; exit 1 }
Write-Host "test_host.exe build OK"
& ..\..\build\test_host.exe
if ($LASTEXITCODE -ne 0) { Write-Error "unit tests failed"; exit 1 }

& powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\tools\test_scripts.ps1
if ($LASTEXITCODE -ne 0) { Write-Error "install/uninstall script checks failed"; exit 1 }
