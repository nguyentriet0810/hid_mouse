param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $projectRoot 'tests\build'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$sources = @(
    (Join-Path $projectRoot 'tests\test_core.c'),
    (Join-Path $projectRoot 'tests\test_ota_policy.c'),
    (Join-Path $projectRoot 'components\ota_policy\ota_policy.c'),
    (Join-Path $projectRoot 'components\mouse_input\mouse_input.c'),
    (Join-Path $projectRoot 'components\mouse_motion\mouse_motion.c')
)
$includes = @('mouse_types','mouse_input','mouse_motion','ota_policy') | ForEach-Object {
    '-I' + (Join-Path $projectRoot "components\$_\include")
}
$testExe = Join-Path $testBuild 'test_core.exe'
& $Compiler -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion `
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef `
    -Wdouble-promotion -Wcast-qual -Wvla @includes @sources -o $testExe -lm
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }
