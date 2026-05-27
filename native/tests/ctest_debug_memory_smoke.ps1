param(
  [Parameter(Mandatory = $true)]
  [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

$ailang = Join-Path $RepoRoot 'tools/ailang.exe'
if (-not (Test-Path $ailang)) {
  Write-Host "skip: missing $ailang"
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
[void]$builder.AppendLine('    Inst#i1(op=PUSH_INT a=0)')
[void]$builder.AppendLine('    Inst#i2(op=STORE_LOCAL a=0)')
[void]$builder.AppendLine('    Inst#i3(op=CONST a=0)')
[void]$builder.AppendLine('    Inst#i4(op=MAKE_BLOCK)')
[void]$builder.AppendLine('    Inst#i5(op=LOAD_LOCAL a=0)')
[void]$builder.AppendLine('    Inst#i6(op=PUSH_INT a=1)')
[void]$builder.AppendLine('    Inst#i7(op=ADD_INT)')
[void]$builder.AppendLine('    Inst#i8(op=STORE_LOCAL a=0)')
[void]$builder.AppendLine('    Inst#i9(op=LOAD_LOCAL a=0)')
[void]$builder.AppendLine('    Inst#i10(op=PUSH_INT a=17000)')
[void]$builder.AppendLine('    Inst#i11(op=EQ_INT)')
[void]$builder.AppendLine('    Inst#i12(op=JUMP_IF_FALSE a=2)')
[void]$builder.AppendLine('    Inst#i13(op=HALT)')
[void]$builder.AppendLine('  }')
[void]$builder.AppendLine('}')
Set-Content -Path $tmpMemApp -Value $builder.ToString() -NoNewline

& $ailang debug run $tmpMemApp --out $tmpMemOut | Out-Null
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
if ($diag -notmatch 'node_count = 16384') { throw 'debug memory smoke: expected node_count=16384' }
if ($diag -notmatch 'node_high_water = 16384') { throw 'debug memory smoke: expected node_high_water=16384' }
if ($diag -notmatch 'node_gc_pressure_threshold_nodes = 12288') { throw 'debug memory smoke: expected node_gc_pressure_threshold_nodes=12288' }
if ($diag -notmatch 'node_roots = \{') { throw 'debug memory smoke: expected node_roots table' }
if ($diag -notmatch 'node_kind_counts = \[') { throw 'debug memory smoke: expected node_kind_counts in diagnostics' }
if ($diag -notmatch 'kind = "Block"') { throw 'debug memory smoke: expected Block node kind attribution' }
if ($diag -notmatch 'string_arena_pressure_count = 0') { throw 'debug memory smoke: expected string_arena_pressure_count in diagnostics' }
if ($diag -notmatch 'bytes_arena_pressure_count = 0') { throw 'debug memory smoke: expected bytes_arena_pressure_count in diagnostics' }
if ($diag -notmatch 'node_arena_pressure_count = [1-9][0-9]*') { throw 'debug memory smoke: expected node_arena_pressure_count>0 in diagnostics' }
if ($snap -notmatch 'node_gc_attempts = [1-9][0-9]*') { throw 'debug memory smoke: expected node_gc_attempts>0 in state snapshots' }
if ($snap -notmatch 'node_root_stack_slots') { throw 'debug memory smoke: expected node_root_stack_slots in state snapshots' }
if ($snap -notmatch 'node_arena_pressure_count = [1-9][0-9]*') { throw 'debug memory smoke: expected node_arena_pressure_count>0 in state snapshots' }

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

& $ailang debug run $tmpOkApp --out $tmpOkOut | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'debug memory smoke: expected successful debug run' }
$okConfig = Get-Content -Raw (Join-Path $tmpOkOut 'config.toml')
$okDiag = Get-Content -Raw (Join-Path $tmpOkOut 'diagnostics.toml')
$okSnap = Get-Content -Raw (Join-Path $tmpOkOut 'state_snapshots.toml')
if ($okConfig -notmatch 'status = "ok"') { throw 'debug memory smoke: expected status=ok in config.toml' }
if ($okDiag -notmatch 'vm_code=AIVM000') { throw 'debug memory smoke: expected vm_code=AIVM000' }
if ($okDiag -notmatch 'node_gc_attempts = [0-9]+') { throw 'debug memory smoke: expected node_gc_attempts field in diagnostics' }
if ($okSnap -notmatch 'node_root_stack_slots') { throw 'debug memory smoke: expected node_root_stack_slots in success snapshots' }
if ($okDiag -notmatch 'node_roots = \{') { throw 'debug memory smoke: expected node_roots table in success diagnostics' }
if ($okDiag -notmatch 'node_kind_counts = \[') { throw 'debug memory smoke: expected node_kind_counts in success diagnostics' }
if ($okDiag -notmatch 'node_arena_pressure_count = 0') { throw 'debug memory smoke: expected node_arena_pressure_count=0 in success diagnostics' }

Write-Host 'debug memory smoke: PASS'
