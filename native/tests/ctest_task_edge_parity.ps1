param(
  [Parameter(Mandatory = $true)]
  [string]$RepoRoot,
  [Parameter(Mandatory = $true)]
  [string]$ParityCli
)

$ErrorActionPreference = 'Stop'

$airun = Join-Path $RepoRoot 'tools/airun.exe'
if (-not (Test-Path $airun)) {
  Write-Host "skip: missing $airun"
  exit 0
}
if (-not (Test-Path $ParityCli)) {
  throw "missing parity cli: $ParityCli"
}

$tmp = Join-Path $RepoRoot '.tmp/ctest-task-edge-parity-win'
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Run-Case {
  param(
    [string]$Name,
    [string]$Input,
    [string]$Expected,
    [int]$ExpectedExit
  )

  $actual = Join-Path $tmp "$Name.out"
  & $airun run $Input --vm=c 1>$actual 2>&1
  $actualExit = $LASTEXITCODE
  if ($actualExit -ne $ExpectedExit) {
    throw "task edge parity mismatch ($Name): exit $actualExit expected $ExpectedExit"
  }
  if ([string]::IsNullOrEmpty($Expected)) {
    $content = if (Test-Path $actual) { Get-Content -Raw $actual } else { '' }
    if (-not [string]::IsNullOrEmpty($content)) {
      throw "task edge parity mismatch ($Name): expected empty output"
    }
  } else {
    & $ParityCli $actual $Expected | Out-Null
    if ($LASTEXITCODE -ne 0) {
      throw "task edge parity mismatch ($Name): output differs"
    }
  }
}

Run-Case 'await_edge_invalid' `
  (Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_await_edge_invalid.aos') `
  (Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_await_edge_invalid.out') `
  3
Run-Case 'par_join_edge_invalid' `
  (Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_join_edge_invalid.aos') `
  (Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_join_edge_invalid.out') `
  3
Run-Case 'par_cancel_edge_noop' `
  (Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_par_cancel_edge_noop.aos') `
  '' `
  0

Write-Host 'task edge parity: PASS'
