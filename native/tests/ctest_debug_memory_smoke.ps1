param(
  [Parameter(Mandatory = $true)]
  [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

$airun = Join-Path $RepoRoot 'tools/airun.exe'
if (-not (Test-Path $airun)) {
  Write-Host "skip: missing $airun"
  exit 0
}

$tmpMemDir = Join-Path $RepoRoot '.tmp/ctest-debug-memory-win'
$tmpMemOut = Join-Path $RepoRoot '.tmp/ctest-debug-memory-out-win'
$tmpMemApp = Join-Path $tmpMemDir 'memory_pressure.aos'
if (Test-Path $tmpMemDir) { Remove-Item -Recurse -Force $tmpMemDir }
if (Test-Path $tmpMemOut) { Remove-Item -Recurse -Force $tmpMemOut }
New-Item -ItemType Directory -Force -Path $tmpMemDir | Out-Null

$builder = New-Object System.Text.StringBuilder
[void]$builder.AppendLine('Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {')
[void]$builder.AppendLine('  Const#k0(kind=string value="n")')
[void]$builder.AppendLine('  Func#f1(name=main params="argv" locals="") {')
$instId = 1
for ($n = 1; $n -le 600; $n++) {
  [void]$builder.AppendLine(("    Inst#c{0}(op=CONST a=0)" -f $instId))
  $instId++
  [void]$builder.AppendLine(("    Inst#m{0}(op=MAKE_BLOCK)" -f $instId))
  $instId++
}
[void]$builder.AppendLine(("    Inst#h{0}(op=HALT)" -f $instId))
[void]$builder.AppendLine('  }')
[void]$builder.AppendLine('}')
Set-Content -Path $tmpMemApp -Value $builder.ToString() -NoNewline

& $airun debug run $tmpMemApp --out $tmpMemOut | Out-Null
if ($LASTEXITCODE -eq 0) {
  throw 'debug memory smoke: expected memory-pressure failure'
}
if (-not (Test-Path (Join-Path $tmpMemOut 'diagnostics.toml')) -or
    -not (Test-Path (Join-Path $tmpMemOut 'state_snapshots.toml')) -or
    -not (Test-Path (Join-Path $tmpMemOut 'config.toml'))) {
  throw 'debug memory smoke: expected debug artifacts missing'
}
$config = Get-Content -Raw (Join-Path $tmpMemOut 'config.toml')
$diag = Get-Content -Raw (Join-Path $tmpMemOut 'diagnostics.toml')
$snap = Get-Content -Raw (Join-Path $tmpMemOut 'state_snapshots.toml')
if ($config -notmatch 'status = "error"') { throw 'debug memory smoke: expected status=error in config.toml' }
if ($diag -notmatch 'vm_code=AIVM011') { throw 'debug memory smoke: expected vm_code=AIVM011' }
if ($diag -notmatch 'detail=(AIVMM005: )?node arena capacity exceeded\.') { throw 'debug memory smoke: expected node arena capacity detail' }
if ($diag -notmatch 'node_gc_compactions = [1-9][0-9]*') { throw 'debug memory smoke: expected gc compaction activity' }
if ($diag -notmatch 'node_gc_attempts = [1-9][0-9]*') { throw 'debug memory smoke: expected gc attempt activity' }
if ($diag -notmatch 'node_count = 512') { throw 'debug memory smoke: expected node_count=512' }
if ($diag -notmatch 'node_high_water = 512') { throw 'debug memory smoke: expected node_high_water=512' }
if ($diag -notmatch 'node_gc_pressure_threshold_nodes = 384') { throw 'debug memory smoke: expected node_gc_pressure_threshold_nodes=384' }
if ($diag -notmatch 'node_roots = \{') { throw 'debug memory smoke: expected node_roots table' }
if ($snap -notmatch 'node_gc_attempts = [1-9][0-9]*') { throw 'debug memory smoke: expected node_gc_attempts>0 in state snapshots' }
if ($snap -notmatch 'node_root_stack_slots') { throw 'debug memory smoke: expected node_root_stack_slots in state snapshots' }

$tmpOkDir = Join-Path $RepoRoot '.tmp/ctest-debug-ok-win'
$tmpOkOut = Join-Path $RepoRoot '.tmp/ctest-debug-ok-out-win'
$tmpOkApp = Join-Path $tmpOkDir 'success_path.aos'
if (Test-Path $tmpOkDir) { Remove-Item -Recurse -Force $tmpOkDir }
if (Test-Path $tmpOkOut) { Remove-Item -Recurse -Force $tmpOkOut }
New-Item -ItemType Directory -Force -Path $tmpOkDir | Out-Null
@"
Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {
  Func#f1(name=main params="argv" locals="") {
    Inst#i1(op=HALT)
  }
}
"@ | Set-Content -Path $tmpOkApp -NoNewline

& $airun debug run $tmpOkApp --out $tmpOkOut | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'debug memory smoke: expected successful debug run' }
$okConfig = Get-Content -Raw (Join-Path $tmpOkOut 'config.toml')
$okDiag = Get-Content -Raw (Join-Path $tmpOkOut 'diagnostics.toml')
$okSnap = Get-Content -Raw (Join-Path $tmpOkOut 'state_snapshots.toml')
if ($okConfig -notmatch 'status = "ok"') { throw 'debug memory smoke: expected status=ok in config.toml' }
if ($okDiag -notmatch 'vm_code=AIVM000') { throw 'debug memory smoke: expected vm_code=AIVM000' }
if ($okDiag -notmatch 'node_gc_attempts = 0') { throw 'debug memory smoke: expected node_gc_attempts=0 in diagnostics' }
if ($okSnap -notmatch 'node_root_stack_slots') { throw 'debug memory smoke: expected node_root_stack_slots in success snapshots' }
if ($okDiag -notmatch 'node_roots = \{') { throw 'debug memory smoke: expected node_roots table in success diagnostics' }

Write-Host 'debug memory smoke: PASS'
