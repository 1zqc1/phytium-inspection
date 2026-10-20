$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
$simulatorExe = Join-Path $buildDir 'simulator.exe'
$includeDir = Join-Path $projectRoot 'common\include'
$sourceDir = Join-Path $projectRoot 'common\src'
$toolDir = Join-Path $projectRoot 'tools'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$sources = @(Get-ChildItem -Path $sourceDir -Filter '*.c' -File | ForEach-Object { $_.FullName })

& gcc -std=c99 -Wall -Wextra -Werror -pedantic -DFI_SIMULATOR_MAIN -I $includeDir -I $toolDir @sources (Join-Path $toolDir 'simulator.c') -o $simulatorExe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $simulatorExe
exit $LASTEXITCODE

