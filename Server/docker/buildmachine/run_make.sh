cd /src/third_party/flatbuffers
cmake -S . -B LinuxBuilds -G "Unix Makefiles"
cmake --build LinuxBuilds/
cd /src
cmake -S . -B LinuxBuilds -G "Unix Makefiles"
cmake --build LinuxBuilds
