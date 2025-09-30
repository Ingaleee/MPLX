param(
  [string]$Preset = "dev"
)

$ErrorActionPreference = "Stop"

function Run-Bench($jitMode, $file, $runs) {
  $cli = Join-Path "build/$Preset" "Presentation/tools/mplx/mplx.exe"
  if (-not (Test-Path $cli)) { throw "CLI not found at $cli" }
  Write-Host "== bench: $file (jit=$jitMode, runs=$runs) ==" -ForegroundColor Cyan
  & $cli --bench $file --mode run-only --runs $runs --json --jit $jitMode | Write-Host
}

function Run-Run($jitMode, $file) {
  $cli = Join-Path "build/$Preset" "Presentation/tools/mplx/mplx.exe"
  & $cli --run $file --jit $jitMode --hot 1 | Write-Host
}

Write-Host "== LOOP: 20-50M iterations, JIT vs no-JIT ==" -ForegroundColor Green
Run-Bench off  "Presentation/examples/loop.mplx" 3
Run-Bench on   "Presentation/examples/loop.mplx" 3

Write-Host "== SUM/FIB correctness and timing ==" -ForegroundColor Green
Run-Run on  "Presentation/examples/sum.mplx"
Run-Run on  "Presentation/examples/fib.mplx"

Write-Host "== JIT dump sample (first function) ==" -ForegroundColor Green
$env:MPLX_JIT_DUMP = "1"
Run-Run on  "Presentation/examples/simple.mplx"
$env:MPLX_JIT_DUMP = "0"

Write-Host "== Mixed scene: JIT caller to interpreted callee ==" -ForegroundColor Green
# You can prepare an example where main calls a helper with unsupported op; for demo use fib which may fallback.
Run-Run auto "Presentation/examples/fib.mplx"


