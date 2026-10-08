param(
    [string]$BuildDir = 'out/build/ci-windows',
    [string]$DependencyDir = 'out/ci/deps/vcpkg'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
# Run from the checkout root. Pin tool and port registry to the same repository baseline.
$baseline = (Get-Content -LiteralPath 'vcpkg-configuration.json' -Raw | ConvertFrom-Json).'default-registry'.baseline
if ($baseline -notmatch '^[0-9a-f]{40}$') { throw 'Invalid vcpkg baseline' }
New-Item -ItemType Directory -Force -Path $DependencyDir | Out-Null
$dependencyPath = (Resolve-Path -LiteralPath $DependencyDir).Path
if (-not (Test-Path -LiteralPath "$dependencyPath/.git")) {
    # Keep history: the baseline's version database references older port trees.
    Invoke-Checked git @('clone', 'https://github.com/microsoft/vcpkg.git', $dependencyPath)
    Invoke-Checked git @('-C', $dependencyPath, 'checkout', '--detach', $baseline)
}
$actual = & git -C $dependencyPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -ne $baseline) { throw 'Dependency checkout differs from pinned baseline' }
Invoke-Checked "$dependencyPath/bootstrap-vcpkg.bat" @('-disableMetrics')
$env:VCPKG_DISABLE_METRICS = '1'
Invoke-Checked cmake @('-S', '.', '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_TOOLCHAIN_FILE=$dependencyPath/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows')
