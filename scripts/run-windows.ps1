$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$viewerPath = Join-Path $projectRoot 'build/windows-cpu/Release/cloudforge_viewer.exe'
if (-not (Test-Path -LiteralPath $viewerPath)) {
    throw 'Build first: cmake --preset windows-cpu-release; cmake --build --preset windows-cpu-release'
}
Push-Location $projectRoot
try { & $viewerPath @args; exit $LASTEXITCODE }
finally { Pop-Location }
