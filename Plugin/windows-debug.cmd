@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "JAMMERNETZ_ROOT=%%~fI"
set "JAMMERNETZ_BUILD=%JAMMERNETZ_ROOT%\builds"

if /I "%~1"=="help" goto :usage
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage

call :setup_environment
if errorlevel 1 exit /b %errorlevel%

if "%~1"=="" goto :build
if /I "%~1"=="build" goto :build
if /I "%~1"=="test" goto :test
if /I "%~1"=="server" goto :server
if /I "%~1"=="ableton" goto :ableton
if /I "%~1"=="vs" goto :visual_studio
if /I "%~1"=="env" goto :show_environment
goto :usage_error

:build
cmake --build "%JAMMERNETZ_BUILD%" --config Debug --parallel --target JammerNetzPlugin_VST3 JammerNetzServer JammerNetzPluginTest
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%JAMMERNETZ_BUILD%" -C Debug -R JammerNetzPluginTest --output-on-failure
exit /b %errorlevel%

:test
cmake --build "%JAMMERNETZ_BUILD%" --config Debug --parallel --target JammerNetzPluginTest
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%JAMMERNETZ_BUILD%" -C Debug -R JammerNetzPluginTest --output-on-failure
exit /b %errorlevel%

:server
pushd "%JAMMERNETZ_ROOT%"
"%JAMMERNETZ_BUILD%\Server\Debug\JammerNetzServer.exe" -k "%JAMMERNETZ_ROOT%\RandomNumbers.bin" --port=7777
set "JAMMERNETZ_RESULT=%errorlevel%"
popd
exit /b %JAMMERNETZ_RESULT%

:ableton
set "ABLETON_EXE=%~2"
if defined ABLETON_EXE goto :start_ableton
set "ABLETON_EXE=%JAMMERNETZ_ABLETON_EXE%"
:start_ableton
if not defined ABLETON_EXE goto :ableton_not_found
if not exist "%ABLETON_EXE%" goto :ableton_not_found
start "" "%ABLETON_EXE%"
exit /b 0
:ableton_not_found
echo Ableton executable not configured or not found.
echo Pass its full path as the second argument:
echo   Plugin\windows-debug.cmd ableton "C:\path\to\Ableton Live.exe"
echo Or set JAMMERNETZ_ABLETON_EXE to that full path.
exit /b 1

:visual_studio
set "DEVENV_EXE=%VS_INSTALL%\Common7\IDE\devenv.exe"
if not exist "%DEVENV_EXE%" (
	echo Visual Studio executable not found at "%DEVENV_EXE%".
	exit /b 1
)
start "" "%DEVENV_EXE%" "%JAMMERNETZ_BUILD%\JammerNetz.sln"
exit /b 0

:show_environment
exit /b 0

:setup_environment
if not exist "%JAMMERNETZ_BUILD%\CMakeCache.txt" (
	echo Configure the CMake build directory first: "%JAMMERNETZ_BUILD%".
	exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo Could not find vswhere.exe. Install Visual Studio with the Desktop development with C++ workload.
	exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
	echo Could not locate a Visual Studio C++ installation.
	exit /b 1
)

set "DEBUG_CRT="
for /d %%V in ("%VS_INSTALL%\VC\Redist\MSVC\*") do (
	for /d %%D in ("%%~fV\debug_nonredist\x64\Microsoft.VC*.DebugCRT") do (
		if exist "%%~fD\VCRUNTIME140D.dll" set "DEBUG_CRT=%%~fD"
	)
)
if not defined DEBUG_CRT (
	echo Could not locate the x64 MSVC Debug CRT below "%VS_INSTALL%\VC\Redist".
	exit /b 1
)

:environment_ready
set "PATH=%DEBUG_CRT%;%PATH%"
echo Using Debug CRT: %DEBUG_CRT%
exit /b 0

:usage_error
echo Unknown action: %~1
call :print_usage
exit /b 2
:usage
call :print_usage
exit /b 0

:print_usage
echo Usage: Plugin\windows-debug.cmd ACTION [ARGUMENT]
echo.
echo   build              Build the Debug VST3, server, and tests; then run the tests.
echo   test               Build and run only the plug-in tests.
echo   server             Run the Debug server on port 7777 with RandomNumbers.bin.
echo   ableton [exe]      Start Ableton with the Debug CRT available to the plug-in.
echo                      The path is required here or in JAMMERNETZ_ABLETON_EXE.
echo   vs                 Open the generated solution with the Debug runtime environment.
echo   env                Locate and print the Debug runtime directories.
echo   help               Show this help.
echo.
echo With no action, the script performs build.
exit /b 0
