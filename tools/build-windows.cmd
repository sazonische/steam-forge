@echo off
setlocal
rem Usage: tools\build-windows.cmd [release|dev]
set MODE=%1
if "%MODE%"=="" set MODE=release

if "%MODE%"=="release" set PRESET=windows-release
if "%MODE%"=="dev" set PRESET=windows-dev
if "%PRESET%"=="" (
	echo Unknown mode "%MODE%". Use release or dev.
	exit /b 1
)

if "%VS_ROOT%"=="" (
	for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%i"
)
if "%VS_ROOT%"=="" (
	echo Visual Studio with the C++ workload was not found. Set VS_ROOT to its installation folder.
	exit /b 1
)

rem vcvars64 replaces VCPKG_ROOT with the copy bundled into Visual Studio, which may not match the manifest baseline.
set STEAMFORGE_VCPKG_ROOT=%VCPKG_ROOT%
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%STEAMFORGE_VCPKG_ROOT%"=="" (set "VCPKG_ROOT=C:/vcpkg") else (set "VCPKG_ROOT=%STEAMFORGE_VCPKG_ROOT%")
set PATH=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
echo Using Visual Studio at %VS_ROOT%
echo Using VCPKG_ROOT=%VCPKG_ROOT%

cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
ctest --preset %PRESET% || exit /b 1
