<#
  Nuke Panel installer.

  Copies build\duke3dmod.dll into <GameDir>\bin, the one folder the patched
  exe's loader stub loads it from: tools/ultrawide_patch.py writes the stub
  into the exe, so there is no separate stub FILE to copy -- what proves the
  stub is there is the exe's SHA256, and that is what this checks first. The
  patcher is byte-reproducible and checks its output against this same hash.

  It NEVER writes duke3dmod.ini. The ini is the player's settings and the DLL
  writes it itself on first change; an installer that seeded one would
  overwrite those settings on every update.

  -ExpectedExeHash and -ProcessName exist for tools/test_scripts.ps1, which
  drives this against a temp directory. Normal use passes neither.
#>
param(
  [Parameter(Mandatory = $true)][string] $GameDir,
  [string] $Dll             = '',
  [string] $ExpectedExeHash = 'A81893835533D0B4A234E014C65E6BE7A00D962D6B9F1992FAADC4469B04D4D0',
  [string] $ProcessName     = 'duke3d'
)
$ErrorActionPreference = 'Stop'
function Fail([string] $why) { Write-Host "REFUSED: $why"; exit 1 }

# THE PROCESS CHECK IS FIRST: copying a DLL over one a running game has mapped fails halfway, and that half crashes the
# next launch. FAIL CLOSED: an enumeration error (WMI hiccup, permissions) is not "no such process"; only
# ProcessCommandException (no such name, under -ErrorAction Stop) is benign.
try {
  $runningProc = Get-Process -Name $ProcessName -ErrorAction Stop
} catch [Microsoft.PowerShell.Commands.ProcessCommandException] {
  $runningProc = $null
} catch {
  Fail "could not check for a running game: $($_.Exception.Message)"
}
if ($runningProc) {
  Fail "$ProcessName is running. Close the game and run this again."
}
# The default DLL path is resolved here, not in param(): under Windows PowerShell 5.1 $PSScriptRoot is empty while
# param() binds defaults for a -File run ('Cannot bind argument to parameter Path'). $PSCommandPath is set in the body
# of both hosts; tools/test_scripts.ps1 drives the real one-liner.
if (-not $Dll) { $Dll = Join-Path (Split-Path -Parent $PSCommandPath) '..\build\duke3dmod.dll' }

if (-not (Test-Path -PathType Container $GameDir)) { Fail "no such directory: $GameDir" }
$binDir = Join-Path $GameDir 'bin'
# JUNCTION DEFENCE: a reparse point at bin means a copy through it lands wherever it points. Checked before any read or
# write through $binDir.
if (Test-Path -PathType Container $binDir) {
  $binItem = Get-Item -Force $binDir
  if ($binItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    Fail "bin is a junction or symbolic link; refusing to write through it"
  }
}
$exe    = Join-Path $binDir 'duke3d.exe'
if (-not (Test-Path -PathType Leaf $exe)) { Fail "no bin\duke3d.exe under $GameDir" }
if (-not (Test-Path -PathType Leaf $Dll)) { Fail "no dll to install at $Dll" }

$exeHash = (Get-FileHash -Algorithm SHA256 $exe).Hash
if ($exeHash -ne $ExpectedExeHash) {
  Write-Host "  expected $ExpectedExeHash"
  Write-Host "  found    $exeHash"
  Fail "bin\duke3d.exe is not the patched exe this build expects. Run tools\ultrawide_patch.py on duke3d.exe.orig first."
}

$srcHash = (Get-FileHash -Algorithm SHA256 $Dll).Hash
$dest    = Join-Path $binDir 'duke3dmod.dll'
# DESTINATION FILE REPARSE-POINT DEFENCE: the check above refuses only a linked bin DIRECTORY; a symlink planted at
# bin\duke3dmod.dll would pass it and Copy-Item would write through it, outside <GameDir>\bin, the one place these
# scripts promise to write. Checked before the copy.
if (Test-Path -PathType Leaf $dest) {
  $destItem = Get-Item -Force $dest
  if ($destItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    Fail "duke3dmod.dll in bin is a symbolic link; refusing to write through it"
  }
}
# A FAILED COPY IS A REFUSED LINE, not a .NET stack trace: a locked destination (antivirus, leftover handle, read-only
# ACL) throws out of Copy-Item, and under 'Stop' that would skip the REFUSED contract every other failure honours.
try {
  Copy-Item -Path $Dll -Destination $dest -Force
} catch {
  Fail "could not copy the dll to $dest`: $($_.Exception.Message)"
}
# VERIFIED AFTER THE COPY: a full disk or a virus scanner holding the file can produce a short copy Copy-Item doesn't
# report.
if (-not (Test-Path -PathType Leaf $dest)) { Fail "the copy did not arrive at $dest" }
$destHash = (Get-FileHash -Algorithm SHA256 $dest).Hash
if ($destHash -ne $srcHash) { Fail "the copy at $dest does not match the source" }

Write-Host "installed duke3dmod.dll -> $dest"
Write-Host "  dll sha256 $destHash"
Write-Host "  exe sha256 $exeHash (patched, loader stub present)"
Write-Host "no ini written; the mod writes duke3dmod.ini itself on the first change."
exit 0
