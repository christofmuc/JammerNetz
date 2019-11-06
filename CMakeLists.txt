#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(JammerNetz)

# We need a little bit support for the JUCE library, and also want to put our own CMake helpers on the module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/juce-cmake/cmake" "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Include useful scripts for CMake
include(FetchContent)

# Configure time downloads of more third_party libraries that can not be maintained as git submodules
# Make sure to read and accept the Steinberg licensing agreement in the directory _deps/asiosdk-src!
FetchContent_Declare(
  asiosdk
  URL https://www.steinberg.net/asiosdk
)
FetchContent_MakeAvailable(asiosdk)

FetchContent_Declare(
	inteltbb
	URL https://github.com/intel/tbb/releases/download/2019_U9/tbb2019_20191006oss_win.zip
)
FetchContent_MakeAvailable(inteltbb)
set(INTEL_TBB_DIRECTORY "${inteltbb_SOURCE_DIR}/tbb2019_20191006oss/") # This includes the version number - use this variable for easy upgrade of TBB

FetchContent_Declare(
	vcredist
	URL https://aka.ms/vs/16/release/vc_redist.x64.exe
	DOWNLOAD_NO_EXTRACT true
)
FetchContent_MakeAvailable(vcredist)
set(VCREDIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/_deps/vcredist-subbuild/vcredist-populate-prefix/src")

# 
# Find CapnProto via CMAKE - strangely, this wants to be compiled as the build output of CapnProto build is the CMake file to include
# 
set(CapnProto_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/capnproto/c++/build/cmake")
find_package(CapnProto REQUIRED) 

# Use all cores
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

# Nothing to be done here on top level, just go one level down 
add_subdirectory("third_party")
add_subdirectory("common")
add_subdirectory("Server")
add_subdirectory("Client")
