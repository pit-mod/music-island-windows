[CmdletBinding()]
param([switch]$VerifyAudio)
$ErrorActionPreference='Stop'
$nativeRoot=$PSScriptRoot
$outputDir=Join-Path $nativeRoot 'build'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $outputDir 'audio-test') | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Visual Studio C++ build tools are required.'}
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Install the Visual Studio Desktop development with C++ workload and Windows SDK.'}
$environmentScript=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$batch=@"
@echo off
call "$environmentScript" >nul
if errorlevel 1 exit /b 1
cd /d "$nativeRoot"
rc /nologo /fo "build\app.res" app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /permissive- /Zc:__cplusplus /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /Fo"build\\" src\Main.cpp src\Media.cpp src\Audio.cpp src\Renderer.cpp src\Keyboard.cpp /Fe"build\MusicIsland.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest build\app.res user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib uiautomationcore.lib uuid.lib windowsapp.lib runtimeobject.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib shlwapi.lib mmdevapi.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fo"build\MotionTests.obj" tests\MotionTests.cpp /Fe"build\MotionTests.exe"
if errorlevel 1 exit /b 1
"build\MotionTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /Fo"build\SpectrumTests.obj" tests\SpectrumTests.cpp /Fe"build\SpectrumTests.exe"
if errorlevel 1 exit /b 1
"build\SpectrumTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fo"build\SettingsTests.obj" tests\SettingsTests.cpp /Fe"build\SettingsTests.exe" /link shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
"build\SettingsTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fo"build\KeyboardTests.obj" tests\KeyboardTests.cpp build\Keyboard.obj /Fe"build\KeyboardTests.exe" /link user32.lib ole32.lib oleaut32.lib uiautomationcore.lib uuid.lib
if errorlevel 1 exit /b 1
"build\KeyboardTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /Fo"build\audio-test\\" tests\AudioIntegrationTests.cpp src\Audio.cpp /Fe"build\AudioIntegrationTests.exe" /link ole32.lib uuid.lib mmdevapi.lib
exit /b %errorlevel%
"@
$batchPath=Join-Path $outputDir 'compile.cmd'
Set-Content -LiteralPath $batchPath -Value $batch -Encoding utf8
& $env:ComSpec /d /c "`"$batchPath`""
if($LASTEXITCODE -ne 0){throw "Native build or regression tests failed (exit $LASTEXITCODE)."}
if($VerifyAudio){& (Join-Path $outputDir 'AudioIntegrationTests.exe');if($LASTEXITCODE -ne 0){throw 'Native audio integration tests failed.'}}
Write-Output "Native app: $(Join-Path $outputDir 'MusicIsland.exe')"
