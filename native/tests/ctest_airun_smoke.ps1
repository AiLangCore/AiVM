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

$tmp = Join-Path $RepoRoot '.tmp/ctest-airun-smoke-win'
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$casePath = Join-Path $RepoRoot 'src/AiVM.Core/native/tests/parity_cases/vm_c_execute_src_main_params.aos'
& $airun run $casePath --vm=c | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'airun smoke: vm=c run failed' }

$publishDir = Join-Path $tmp 'publish-main-params'
$publishErr = Join-Path $tmp 'publish-main-params.err'
& $airun publish $casePath --out $publishDir 1>$null 2>$publishErr
if ($LASTEXITCODE -ne 0) {
  $msg = if (Test-Path $publishErr) { Get-Content -Raw $publishErr } else { '' }
  if ($msg -match 'Failed to copy runtime for target RID') {
    Write-Host 'airun smoke: skipping native publish checks (runtime RID artifact missing)'
    exit 0
  }
  throw 'airun smoke: publish failed'
}
if (-not (Test-Path (Join-Path $publishDir 'app.aibc1'))) { throw 'airun smoke: publish missing app.aibc1' }
if (-not (Test-Path (Join-Path $publishDir 'vm_c_execute_src_main_params.exe'))) { throw 'airun smoke: publish missing executable' }
if ((Test-Path (Join-Path $publishDir 'run.ps1')) -or (Test-Path (Join-Path $publishDir 'run.cmd')) -or (Test-Path (Join-Path $publishDir 'run.sh'))) {
  throw 'airun smoke: unexpected launcher files for native publish'
}
& (Join-Path $publishDir 'vm_c_execute_src_main_params.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'airun smoke: published executable failed' }

$hostRid = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'windows-arm64' } else { 'windows-x64' }
$projectDir = Join-Path $tmp 'project-target'
$projectOut = Join-Path $tmp 'project-target-out'
New-Item -ItemType Directory -Force -Path $projectDir | Out-Null
@"
Program#p1 {
  Project#proj1(name="projtarget" entryFile="main.aos" entryExport="main" publishTarget="$hostRid")
}
"@ | Set-Content -Path (Join-Path $projectDir 'project.aiproj') -NoNewline
@"
Bytecode#bc1(magic="AIBC" format="AiBC1" version=2 flags=0) {
  Func#f1(name=main params="argv" locals="") {
    Inst#i1(op=HALT)
  }
}
"@ | Set-Content -Path (Join-Path $projectDir 'main.aos') -NoNewline

$projectErr = Join-Path $tmp 'project-target.err'
& $airun publish $projectDir --out $projectOut 1>$null 2>$projectErr
if ($LASTEXITCODE -ne 0) {
  $msg = if (Test-Path $projectErr) { Get-Content -Raw $projectErr } else { '' }
  if ($msg -match 'Failed to copy runtime for target RID') {
    Write-Host 'airun smoke: skipping publishTarget manifest check (runtime RID artifact missing)'
  } else {
    throw 'airun smoke: publishTarget manifest publish failed'
  }
} elseif (-not (Test-Path (Join-Path $projectOut 'projtarget.exe'))) {
  throw 'airun smoke: publishTarget manifest missing projtarget.exe'
}

$airunHelp = & $airun 2>&1
if ($airunHelp -match '(^|\r?\n)\s+build(\s|$)' -and $airunHelp -match '(^|\r?\n)\s+clean(\s|$)') {
  $cacheProject = Join-Path $tmp 'cache-project'
  $cacheOutNo = Join-Path $tmp 'cache-out-no'
  $cacheOutYes = Join-Path $tmp 'cache-out-yes'
  New-Item -ItemType Directory -Force -Path $cacheProject | Out-Null
@"
Program#p1 {
  Project#proj1(name="cache_smoke" entryFile="app.aos" entryExport="start")
}
"@ | Set-Content -Path (Join-Path $cacheProject 'project.aiproj') -NoNewline
@"
Program#p1 {
  Export#e1(name=start)
  Let#l1(name=start) {
    Fn#f1(params=argv) {
      Block#b1 {
        Return#r1 { Lit#i1(value=0) }
      }
    }
  }
}
"@ | Set-Content -Path (Join-Path $cacheProject 'app.aos') -NoNewline

  & $airun clean $cacheProject | Out-Null
  & $airun build --no-cache $cacheProject --out $cacheOutNo | Out-Null
  if (Get-ChildItem -Path (Join-Path $cacheProject '.toolchain/cache/airun') -Filter app.aibc1 -Recurse -ErrorAction SilentlyContinue) {
    throw 'airun smoke: --no-cache unexpectedly populated cache'
  }
  & $airun build $cacheProject --out $cacheOutYes | Out-Null
  if (-not (Get-ChildItem -Path (Join-Path $cacheProject '.toolchain/cache/airun') -Filter app.aibc1 -Recurse -ErrorAction SilentlyContinue)) {
    throw 'airun smoke: cached build did not write cache artifact'
  }
  & $airun clean $cacheProject | Out-Null
  if (Get-ChildItem -Path (Join-Path $cacheProject '.toolchain/cache/airun') -Filter app.aibc1 -Recurse -ErrorAction SilentlyContinue) {
    throw 'airun smoke: clean did not remove cache artifact'
  }
}

Write-Host 'airun smoke: PASS'
