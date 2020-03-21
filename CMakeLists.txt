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
IF (WIN32)
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
ELSE()
	ADD_DEFINITIONS(-DJUCE_JACK)

	# Include useful scripts for CMake
	find_package(PkgConfig REQUIRED) 

	# These calls create special `PkgConfig::<MODULE>` variables
	pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
	pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.0)
	pkg_check_modules(GLEW REQUIRED IMPORTED_TARGET glew)
ENDIF()

#
# We need a cryptographic key that server and client will share so they can communicate. In its simplest form, we will just take some random numbers from the internet
set(CRYPTO_FILE "${CMAKE_CURRENT_LIST_DIR}/common/resources/RandomNumbers.bin")
if(NOT EXISTS ${CRYPTO_FILE})
	message(STATUS "Creating 72 random byte crypto seed with the help of random.org")
	file(DOWNLOAD "https://www.random.org/cgi-bin/randbyte?nbytes=72&format=f" ${CRYPTO_FILE}
		   TIMEOUT 60  # seconds
		  )
endif()

# Use all cores
IF (MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

# Nothing to be done here on top level, just go one level down 
add_subdirectory("third_party")
add_subdirectory("modules/juce-utils")
add_subdirectory("common")
add_subdirectory("Server")
add_subdirectory("Client")
