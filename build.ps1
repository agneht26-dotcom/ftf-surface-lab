param([switch]$Bootstrap, [switch]$Run)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$taskTemp = Join-Path $PSScriptRoot 'build\temp'
New-Item -ItemType Directory -Force -Path $taskTemp | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
if ($Bootstrap) {
    & python -B tools/bootstrap.py
    if ($LASTEXITCODE -ne 0) { throw 'Dependency bootstrap failed' }
}
$env:Path += ';C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoots = & $vswhere -all -products '*' -property installationPath
$devScript = $vsRoots | ForEach-Object { Join-Path $_ 'Common7\Tools\VsDevCmd.bat' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $devScript) { throw 'Visual C++ build tools not found' }
$environmentBatch = Join-Path $PSScriptRoot 'build\dev-env.cmd'
@("@call `"$devScript`" -arch=x64 -host_arch=x64 >nul", '@if errorlevel 1 exit /b 1', '@set') | Set-Content -LiteralPath $environmentBatch -Encoding ascii
$buildEnvironment = & cmd.exe /d /c build\dev-env.cmd
if ($LASTEXITCODE -ne 0) { throw 'Visual C++ environment initialization failed' }
foreach ($entry in $buildEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$cmake = Join-Path $PSScriptRoot '.tools\cmake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Run build.ps1 -Bootstrap first' }
& $cmake -S . -B build -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& $cmake --build build
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
if ($Run) { & (Join-Path $PSScriptRoot 'build\FTFSurface.exe') }
