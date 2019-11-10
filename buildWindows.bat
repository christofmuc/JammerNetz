@if "%VSCMD_VER%" EQU "" (
@echo This command file is meant to be run from within a Developer Command Prompt for VS 2017. 
@echo Please start a VS shell and try again from there!
exit -1
)

rem Two step build - first flatbuffers, then the JammerNetz
cd thirdparty\flatbuffers
cmake -S . -B Builds -G "Visual Studio 15 2017 Win64"
msbuild Builds\FlatBuffers.sln    
cd ..\..
cmake -S . -B Builds -G "Visual Studio 15 2017 Win64"
msbuild builds\JammerNetz.sln /p:Configuration=Debug
msbuild builds\JammerNetz.sln /p:Configuration=Release
