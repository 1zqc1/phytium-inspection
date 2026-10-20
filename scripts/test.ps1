$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
$testExe = Join-Path $buildDir 'native-tests.exe'
$includeDir = Join-Path $projectRoot 'common\include'
$testMain = Join-Path $projectRoot 'common\tests\test_main.c'
$sourceDir = Join-Path $projectRoot 'common\src'
$toolDir = Join-Path $projectRoot 'tools'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$sources = @(Get-ChildItem -Path $sourceDir -Filter '*.c' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })

& gcc -std=c99 -Wall -Wextra -Werror -pedantic -I $includeDir -I $toolDir @sources (Join-Path $toolDir 'simulator.c') $testMain -o $testExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testExe
exit $LASTEXITCODE
