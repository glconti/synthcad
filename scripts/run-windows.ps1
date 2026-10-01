[CmdletBinding()]
param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]] $ViewerArgs
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $env:VCPKG_ROOT) {
  $defaultVcpkgRoot = Join-Path $env:USERPROFILE 'vcpkg'
  if (Test-Path (Join-Path $defaultVcpkgRoot 'scripts\buildsystems\vcpkg.cmake')) {
    $env:VCPKG_ROOT = $defaultVcpkgRoot
  } else {
    throw 'VCPKG_ROOT is not set. Set it to your bootstrapped vcpkg checkout and open a new terminal.'
  }
}
$vcpkgRoot = $env:VCPKG_ROOT

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw 'Visual Studio Build Tools with the C++ workload were not found.'
}
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ninjaDirectory = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$vsDevCmd = Join-Path $vsInstall 'Common7\Tools\VsDevCmd.bat'
if (-not $vsInstall -or -not (Test-Path -LiteralPath $vsDevCmd) -or -not (Test-Path (Join-Path $ninjaDirectory 'ninja.exe'))) {
  throw 'Visual Studio Build Tools with the C++ workload and Ninja were not found.'
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) {
  $cmakeExecutable = $cmake.Source
} else {
  $cmakeExecutable = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if (-not (Test-Path -LiteralPath $cmakeExecutable)) {
    throw 'CMake was not found. Install CMake or Visual Studio Build Tools with the C++ workload.'
  }
}

$vsEnvironment = & cmd.exe /d /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($entry in $vsEnvironment) {
  if ($entry -match '^([^=]+)=(.*)$') {
    Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
  }
}
# VsDevCmd sets its bundled vcpkg path; the project deliberately uses the
# user-managed vcpkg checkout supplied by VCPKG_ROOT instead.
$env:VCPKG_ROOT = $vcpkgRoot
$env:Path = "$ninjaDirectory;$env:Path"

Push-Location $root
try {
  & $cmakeExecutable --preset windows-x64-debug
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  & $cmakeExecutable --build --preset windows-x64-debug
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $viewer = Join-Path $root 'out\build\windows-x64-debug\viewer\dingcad_viewer.exe'
  if (-not (Test-Path -LiteralPath $viewer)) {
    throw "Viewer executable was not found at $viewer"
  }
  & $viewer @ViewerArgs
  exit $LASTEXITCODE
}
finally {
  Pop-Location
}
