$ErrorActionPreference = 'Stop'
$script:passed = 0
$script:failed = 0
function Check([bool] $cond, [string] $what) {
  if ($cond) { $script:passed++ } else { $script:failed++; Write-Host "FAIL $what" }
}

# A FAKE GAME DIRECTORY, built fresh every run and deleted after: these scripts copy and delete files, and a suite
# pointed at a real install could damage one.
function New-FakeGame {
  $root = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_t_" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path (Join-Path $root 'bin') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $root 'gameroot') | Out-Null
  Set-Content -Path (Join-Path $root 'bin\duke3d.exe') -Value 'fake patched exe' -NoNewline
  return $root
}
function Fake-ExeHash([string] $root) {
  return (Get-FileHash -Algorithm SHA256 (Join-Path $root 'bin\duke3d.exe')).Hash
}
function New-FakeDll {
  $p = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_dll_" + [guid]::NewGuid().ToString('N') + ".dll")
  Set-Content -Path $p -Value "fake dll payload $dllMarker" -NoNewline
  return $p
}

# uninstall.ps1 deletes only a dll carrying this string; dllmain.cpp exports it (checked against the real build below).
$dllMarker = 'NUKEPANEL-DUKE3DMOD'
$install   = Join-Path $PSScriptRoot 'install.ps1'
$uninstall = Join-Path $PSScriptRoot 'uninstall.ps1'
# A happy-path install check that does not override -ProcessName depends on the real default ('duke3d') not running, so
# it would fail while duke3d.exe runs, for the wrong reason. Every install call expecting SUCCESS passes an absent name.
$noProc = 'nukepanel_no_such_process'

# the happy path
$g = New-FakeGame; $dll = New-FakeDll
& $install -GameDir $g -Dll $dll -ExpectedExeHash (Fake-ExeHash $g) -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0) 'install exits 0 on a matching exe'
$dest = Join-Path $g 'bin\duke3dmod.dll'
Check (Test-Path $dest) 'install copied the dll into bin'
# COPIED BY HASH, not by name: a truncated or half-written copy is the failure this catches, and "Test-Path" cannot see
# it.
Check ((Get-FileHash -Algorithm SHA256 $dest).Hash -eq (Get-FileHash -Algorithm SHA256 $dll).Hash) `
      'the copy matches the source hash'
# IT NEVER WRITES AN INI. The ini is the player's settings; an installer that wrote one would overwrite them on every
# update.
Check (-not (Test-Path (Join-Path $g 'bin\duke3dmod.ini'))) 'install wrote no ini'
Check (-not (Test-Path (Join-Path $g 'bin\duke3dmod.log'))) 'install wrote no log'
# ... and it touches nothing else in bin.
Check ((Get-ChildItem (Join-Path $g 'bin') -File).Count -eq 2) 'install added exactly one file'

# it is idempotent
& $install -GameDir $g -Dll $dll -ExpectedExeHash (Fake-ExeHash $g) -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0) 'a second install over the first succeeds'
Check ((Get-FileHash -Algorithm SHA256 $dest).Hash -eq (Get-FileHash -Algorithm SHA256 $dll).Hash) `
      'the second install left a good copy'

# uninstall. Every happy path passes -ProcessName $noProc so the suite passes while the real game is running.
Set-Content -Path (Join-Path $g 'bin\duke3dmod.ini') -Value 'menukey=45' -NoNewline
Set-Content -Path (Join-Path $g 'bin\duke3dmod.log') -Value 'Nuke Panel log' -NoNewline
# 6>&1: uninstall.ps1 reports via Write-Host (the Information stream), which a plain "$out = & script" never captures.
# Merge stream 6 so this check can see what the script printed.
$out = & $uninstall -GameDir $g -ExpectedExeHash (Fake-ExeHash $g) -ProcessName $noProc 6>&1
Check ($LASTEXITCODE -eq 0) 'uninstall exits 0'
Check (-not (Test-Path $dest)) 'uninstall removed the dll'
# THE INI AND THE LOG SURVIVE: the whole difference between uninstalling a mod and deleting somebody's settings.
Check (Test-Path (Join-Path $g 'bin\duke3dmod.ini')) 'uninstall kept the ini'
Check (Test-Path (Join-Path $g 'bin\duke3dmod.log')) 'uninstall kept the log'
Check (Test-Path (Join-Path $g 'bin\duke3d.exe')) 'uninstall left the exe alone'
# It SAYS what it removed, by name, rather than exiting silently.
Check (($out -join "`n") -match 'duke3dmod\.dll') 'uninstall named what it removed'
# Uninstalling twice is not an error: nothing to remove is a success.
& $uninstall -GameDir $g -ExpectedExeHash (Fake-ExeHash $g) -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0) 'a second uninstall succeeds'

# Uninstall deletes only the mod's own dll beside the patched exe: a wrong exe or a dll without the marker is refused
# and left in place.
$g4 = New-FakeGame
Set-Content -Path (Join-Path $g4 'bin\duke3dmod.dll') -Value 'somebody else''s dll' -NoNewline
$uout = & $uninstall -GameDir $g4 -ExpectedExeHash (Fake-ExeHash $g4) -ProcessName $noProc 6>&1
Check ($LASTEXITCODE -eq 1) 'uninstall refuses a dll without the mod marker'
Check (($uout -join "`n") -match 'REFUSED') 'and says REFUSED'
Check (Test-Path (Join-Path $g4 'bin\duke3dmod.dll')) 'and kept that dll'
Copy-Item -Path $dll -Destination (Join-Path $g4 'bin\duke3dmod.dll') -Force
& $uninstall -GameDir $g4 -ExpectedExeHash 'DEADBEEF' -ProcessName $noProc 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'uninstall refuses when bin\duke3d.exe is not the patched exe'
Check (Test-Path (Join-Path $g4 'bin\duke3dmod.dll')) 'and kept the dll'
& $uninstall -GameDir $g4 -ProcessName $noProc 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'uninstall with the default hash refuses a fake exe'
# A stock exe never loads the dll, so uninstall accepts it; the refusal names the way forward.
$sout4 = & $uninstall -GameDir $g4 -ExpectedExeHash 'DEADBEEF' -ProcessName $noProc 6>&1
Check (($sout4 -join "`n") -match 'ultrawide_patch\.py' -and ($sout4 -join "`n") -match 'ExpectedExeHash') 'the exe refusal names the way forward'
$g5 = New-FakeGame
Copy-Item -Path $dll -Destination (Join-Path $g5 'bin\duke3dmod.dll') -Force
$fakeStock = Fake-ExeHash $g5
$g5Copy = Join-Path $g5 'u.ps1'
[IO.File]::WriteAllText($g5Copy, [IO.File]::ReadAllText($uninstall).Replace('0782CF3803B847F55EC06C031EB6046C0F9EDD1B49B31DB30673254B99704D45', $fakeStock))
& $g5Copy -GameDir $g5 -ExpectedExeHash 'DEADBEEF' -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0 -and -not (Test-Path (Join-Path $g5 'bin\duke3dmod.dll'))) 'uninstall accepts the stock exe hash and removes the dll'
Remove-Item -Recurse -Force $g5 -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $g4 -ErrorAction SilentlyContinue

# every refusal
$g2 = New-FakeGame
& $install -GameDir $g2 -Dll $dll -ExpectedExeHash 'DEADBEEF' 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'install refuses a wrong exe hash'
Check (-not (Test-Path (Join-Path $g2 'bin\duke3dmod.dll'))) 'a refused install copied nothing'

& $install -GameDir (Join-Path $g2 'nope') -Dll $dll -ExpectedExeHash (Fake-ExeHash $g2) 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'install refuses a game dir that does not exist'

$empty = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_e_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $empty | Out-Null
& $install -GameDir $empty -Dll $dll -ExpectedExeHash (Fake-ExeHash $g2) 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'install refuses a directory with no bin\duke3d.exe'

& $install -GameDir $g2 -Dll (Join-Path $g2 'no-such.dll') -ExpectedExeHash (Fake-ExeHash $g2) 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'install refuses a missing dll'
Check (-not (Test-Path (Join-Path $g2 'bin\duke3dmod.dll'))) 'that refusal copied nothing either'

# THE GAME-IS-RUNNING REFUSAL, driven through -ProcessName: pointing it at a process that IS running proves the check
# fires without this suite launching a game or an unknown binary.
$self = (Get-Process -Id $PID).ProcessName
& $install -GameDir $g2 -Dll $dll -ExpectedExeHash (Fake-ExeHash $g2) -ProcessName $self 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'install refuses while the game is running'
Check (-not (Test-Path (Join-Path $g2 'bin\duke3dmod.dll'))) 'and copied nothing'
& $install -GameDir $g2 -Dll $dll -ExpectedExeHash (Fake-ExeHash $g2) -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0) 'and installs once nothing by that name is running'
& $uninstall -GameDir $g2 -ProcessName $self 2>$null | Out-Null
Check ($LASTEXITCODE -eq 1) 'uninstall refuses while the game is running'
Check (Test-Path (Join-Path $g2 'bin\duke3dmod.dll')) 'and removed nothing'

# A junction (or symlink) bin is refused, not written through. A junction at <GameDir>\bin is a reparse point to
# wherever it targets, so copying or deleting through it would write or delete there. Install and uninstall must both
# refuse before touching anything; the junction points at a SEPARATE real folder that must stay empty.
function New-JunctionGame {
  $root   = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_j_" + [guid]::NewGuid().ToString('N'))
  $target = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_jt_" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $root   | Out-Null
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Set-Content -Path (Join-Path $target 'duke3d.exe') -Value 'fake patched exe' -NoNewline
  New-Item -ItemType Junction -Path (Join-Path $root 'bin') -Target $target | Out-Null
  return @{ root = $root; target = $target }
}
$jg = New-JunctionGame
$jgExeHash = (Get-FileHash -Algorithm SHA256 (Join-Path $jg.target 'duke3d.exe')).Hash
$jout = & $install -GameDir $jg.root -Dll $dll -ExpectedExeHash $jgExeHash -ProcessName $noProc 6>&1
Check ($LASTEXITCODE -eq 1) 'install refuses a junction bin'
Check (($jout -join "`n") -match 'REFUSED.*junction') 'and says why, by name'
Check (-not (Test-Path (Join-Path $jg.target 'duke3dmod.dll'))) 'the junction target was never written through'
$jout2 = & $uninstall -GameDir $jg.root -ExpectedExeHash $jgExeHash -ProcessName $noProc 6>&1
Check ($LASTEXITCODE -eq 1) 'uninstall refuses a junction bin'
Check (($jout2 -join "`n") -match 'REFUSED.*junction') 'and says why, by name, too'
Remove-Item -Recurse -Force $jg.root, $jg.target -ErrorAction SilentlyContinue

# bin\duke3dmod.dll ITSELF as a symlink to somewhere outside bin: the junction check only guards the bin DIRECTORY, so
# this proves install refuses before Copy-Item follows a linked destination FILE. New-Item -ItemType SymbolicLink needs
# Developer Mode or elevation; if creation fails the check is skipped rather than failing for an environment reason.
$sg = New-FakeGame
$symTarget = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_symtarget_" + [guid]::NewGuid().ToString('N') + ".dll")
Set-Content -Path $symTarget -Value 'pre-existing file outside bin' -NoNewline
$symDest = Join-Path $sg 'bin\duke3dmod.dll'
$symOk = $true
try {
  New-Item -ItemType SymbolicLink -Path $symDest -Target $symTarget -ErrorAction Stop | Out-Null
} catch {
  $symOk = $false
}
if ($symOk) {
  $sout = & $install -GameDir $sg -Dll $dll -ExpectedExeHash (Fake-ExeHash $sg) -ProcessName $noProc 6>&1
  Check ($LASTEXITCODE -eq 1) 'install refuses a symlinked bin\duke3dmod.dll'
  Check (($sout -join "`n") -match 'REFUSED.*symbolic link') 'and says why, by name'
  Check ((Get-Content -Raw $symTarget) -eq 'pre-existing file outside bin') 'the link target was never written through'
} else {
  Write-Host "SKIPPED: bin\duke3dmod.dll symlink check (New-Item -ItemType SymbolicLink needs Developer Mode or elevation)"
}
Remove-Item -Force $symDest -ErrorAction SilentlyContinue
Remove-Item -Force $symTarget -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $sg -ErrorAction SilentlyContinue

# A failed copy or delete is a REFUSED line and exit 1, not a raw exception trace. The realistic case is a locked
# destination (antivirus or a leftover handle on bin\duke3dmod.dll). This locks the file IN THIS PROCESS
# (FileShare.None), enough to make Copy-Item throw, then checks the script turned that into a REFUSED line.
$g3 = New-FakeGame
& $install -GameDir $g3 -Dll $dll -ExpectedExeHash (Fake-ExeHash $g3) -ProcessName $noProc | Out-Null
Check ($LASTEXITCODE -eq 0) 'setup: the first install onto g3 succeeds'
$g3dest = Join-Path $g3 'bin\duke3dmod.dll'
$lockStream = [System.IO.File]::Open($g3dest, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
try {
  $lout = & $install -GameDir $g3 -Dll $dll -ExpectedExeHash (Fake-ExeHash $g3) -ProcessName $noProc 6>&1
  Check ($LASTEXITCODE -eq 1) 'install refuses when the destination is locked'
  Check (($lout -join "`n") -match 'REFUSED') 'and reports REFUSED, not a raw trace'
} finally {
  $lockStream.Close()
}
Remove-Item -Recurse -Force $g3 -ErrorAction SilentlyContinue

# THE DOCUMENTED ONE-LINER, WITH NO -Dll. Under "powershell -File install.ps1 -GameDir <dir>" $PSScriptRoot is EMPTY
# while param() binds defaults, so a Join-Path default dies before the body runs; the default is resolved in the BODY
# from $PSCommandPath. Driven through the real hosts, since an in-process "& $install" cannot see it.
function Invoke-Quiet([string] $exe, [string[]] $argv) {
  # NATIVE stderr becomes an ErrorRecord in Windows PowerShell once redirected, and under $ErrorActionPreference =
  # 'Stop' that is TERMINATING and would abort this suite on the failure the checks report. Merge stream 2 and lower the
  # preference for the call, so a broken install.ps1 gives a FAIL line instead of a stack trace.
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { & $exe @argv 2>&1 | Out-Null } finally { $ErrorActionPreference = $prev }
  return $LASTEXITCODE
}
function New-FakeRepo {
  $root = Join-Path ([System.IO.Path]::GetTempPath()) ("nukepanel_r_" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path (Join-Path $root 'tools') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
  Copy-Item -Path $install   -Destination (Join-Path $root 'tools\install.ps1')
  Copy-Item -Path $uninstall -Destination (Join-Path $root 'tools\uninstall.ps1')
  Set-Content -Path (Join-Path $root 'build\duke3dmod.dll') -Value 'default path payload' -NoNewline
  return $root
}
$repo        = New-FakeRepo
$repoInstall = Join-Path $repo 'tools\install.ps1'
$repoDllHash = (Get-FileHash -Algorithm SHA256 (Join-Path $repo 'build\duke3dmod.dll')).Hash
# Windows PowerShell 5.1 is the host the README's one-liner names, so it must pass; pwsh 7 is checked too when present,
# because the two bind the automatic path variables differently.
$shells = @(@{ name = 'powershell 5.1'; exe = 'powershell'; pre = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File') })
if (Get-Command pwsh -ErrorAction SilentlyContinue) {
  $shells += @{ name = 'pwsh 7'; exe = 'pwsh'; pre = @('-NoProfile', '-File') }
}
foreach ($sh in $shells) {
  $gd = New-FakeGame
  $rc = Invoke-Quiet $sh.exe ($sh.pre + @($repoInstall, '-GameDir', $gd, '-ExpectedExeHash', (Fake-ExeHash $gd), '-ProcessName', $noProc))
  Check ($rc -eq 0) "install with no -Dll exits 0 under $($sh.name) -File"
  $gdDest = Join-Path $gd 'bin\duke3dmod.dll'
  Check (Test-Path -PathType Leaf $gdDest) "install with no -Dll resolved build\duke3dmod.dll under $($sh.name) -File"
  $gdHash = if (Test-Path -PathType Leaf $gdDest) { (Get-FileHash -Algorithm SHA256 $gdDest).Hash } else { '' }
  Check ($gdHash -eq $repoDllHash) "the default-path copy is build\duke3dmod.dll under $($sh.name) -File"
  Remove-Item -Recurse -Force $gd -ErrorAction SilentlyContinue
}
Remove-Item -Recurse -Force $repo -ErrorAction SilentlyContinue

# the recorded hash is the real one: not a placeholder or zero, and the right shape and length for SHA256.
$recorded = (Select-String -Path $install -Pattern "ExpectedExeHash\s*=\s*'([0-9A-Fa-f]{64})'").Matches[0].Groups[1].Value
Check ($recorded.Length -eq 64) 'the recorded hash is 64 hex characters'
Check ($recorded -eq 'A81893835533D0B4A234E014C65E6BE7A00D962D6B9F1992FAADC4469B04D4D0') `
      'the recorded hash is the patched exe hash (tools\test_patch.py proves it is the patcher output)'
$recordedU = (Select-String -Path $uninstall -Pattern "ExpectedExeHash\s*=\s*'([0-9A-Fa-f]{64})'").Matches
Check ($recordedU.Count -eq 1 -and $recordedU[0].Groups[1].Value -eq $recorded) 'uninstall.ps1 records the same exe hash'

# The real build carries the marker uninstall.ps1 looks for (build.ps1 builds the dll before running this suite).
$builtDll = Join-Path $PSScriptRoot '..\build\duke3dmod.dll'
$builtText = if (Test-Path -PathType Leaf $builtDll) {
  [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($builtDll))
} else { '' }
Check ($builtText.Contains($dllMarker)) 'build\duke3dmod.dll carries the uninstall marker'

Remove-Item -Recurse -Force $g, $g2, $empty -ErrorAction SilentlyContinue
Remove-Item -Force $dll -ErrorAction SilentlyContinue
Write-Host "$script:passed passed, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
