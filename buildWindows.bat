rem Two step build - first flatbuffers, then the JammerNetz
cd third_party\flatbuffers
cmake -S . -B Builds -G "Visual Studio 15 2017 Win64"
cmake --build Builds
cd ..\..
cmake -S . -B Builds\Windows -G "Visual Studio 15 2017 Win64"
cmake --build Builds\Windows  --config Debug
cmake --build Builds\Windows  --config Release
