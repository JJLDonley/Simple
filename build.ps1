param(
  [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

cmake -S $root -B $build
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
cmake --build $build --config $Config
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$candidates = @(
  (Join-Path $build "bin/simplevm_tests.exe"),
  (Join-Path $build "bin/simplevm_tests_all.exe"),
  (Join-Path $build "bin/$Config/simplevm_tests.exe"),
  (Join-Path $build "bin/$Config/simplevm_tests_all.exe"),
  (Join-Path $build "$Config/simplevm_tests.exe"),
  (Join-Path $build "$Config/simplevm_tests_all.exe")
)

$testExe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $testExe) {
  Write-Error "simplevm_tests.exe not found in build output."
}

& $testExe
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
