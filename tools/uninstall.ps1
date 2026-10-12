<#
  Nuke Panel uninstaller.

  Removes duke3dmod.dll from <GameDir>\bin and LEAVES THE INI AND THE LOG,
  which are the player's settings and their diagnostics, not the mod. It
  deletes the dll only beside the patched exe and only when the file carries
  the mod's marker, so a mistyped GameDir never loses somebody else's file.

  It also leaves the exe patched, deliberately: the loader stub lives inside
  it, and without the DLL present the patched exe runs the game stock (see the
  README). Reverting the exe as well is one more step the player takes on
  purpose -- copy bin\duke3d.exe.orig over bin\duke3d.exe -- and it also
  removes the ultrawide fix, which is why this script does not do it for them.
#>
param(
  [Parameter(Mandatory = $true)][string] $GameDir,
  [string] $ExpectedExeHash = 'A81893835533D0B4A234E014C65E6BE7A00D962D6B9F1992FAADC4469B04D4D0',
  [string] $ProcessName     = 'duke3d'
)
# dllmain.cpp exports this string; tools/test_scripts.ps1 checks the real build carries it.
$StockExeHash = '0782CF3803B847F55EC06C031EB6046C0F9EDD1B49B31DB30673254B99704D45'   # ultrawide_patch.py STOCK_SHA256
$DllMarker = 'NUKEPANEL-DUKE3DMOD'
$ErrorActionPreference = 'Stop'
function Fail([string] $why) { Write-Host "REFUSED: $why"; exit 1 }

# FAIL CLOSED, as install.ps1: a Get-Process enumeration error is not "no such process" and must not be swallowed.
# ProcessCommandException is the one benign case; everything else is reported.
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
if (-not (Test-Path -PathType Container $GameDir)) { Fail "no such directory: $GameDir" }
$binDir = Join-Path $GameDir 'bin'
# Junction defence, as install.ps1: refuse before any read or delete through $binDir.
if (Test-Path -PathType Container $binDir) {
  $binItem = Get-Item -Force $binDir
  if ($binItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    Fail "bin is a junction or symbolic link; refusing to write through it"
  }
}
$dll    = Join-Path $binDir 'duke3dmod.dll'

$removed = @()
if (Test-Path -PathType Leaf $dll) {
  $exe = Join-Path $binDir 'duke3d.exe'
  $exeHash = if (Test-Path -PathType Leaf $exe) { (Get-FileHash -Algorithm SHA256 $exe).Hash } else { '' }
  # A stock exe never loads the dll, so removing the dll beside it is safe too.
  if ($exeHash -ne $ExpectedExeHash -and $exeHash -ne $StockExeHash) {
    Fail ("bin\duke3d.exe is neither the patched exe this build expects nor the stock exe; not deleting $dll. " +
          "Re-patch with the current tools\ultrawide_patch.py, or pass -ExpectedExeHash with this exe's SHA-256.")
  }
  $dllText = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($dll))
  if (-not $dllText.Contains($DllMarker)) {
    Fail "$dll is not the Nuke Panel dll (no marker); not deleting it"
  }
  # A failed delete is a REFUSED line, not a raw trace: a locked or read-only dll throws under 'Stop'.
  try {
    Remove-Item -Force $dll
  } catch {
    Fail "could not remove $dll`: $($_.Exception.Message)"
  }
  $removed += 'duke3dmod.dll'
}

if ($removed.Count -eq 0) {
  Write-Host "nothing to remove: duke3dmod.dll is not in $binDir"
} else {
  foreach ($r in $removed) { Write-Host "removed $r" }
}
foreach ($kept in @('duke3dmod.ini', 'duke3dmod.log')) {
  if (Test-Path -PathType Leaf (Join-Path $binDir $kept)) { Write-Host "kept $kept" }
}
Write-Host "bin\duke3d.exe is left patched; without the dll it runs the game stock."
Write-Host "to revert the exe as well (this also removes the ultrawide fix):"
Write-Host "  copy bin\duke3d.exe.orig bin\duke3d.exe"
exit 0
