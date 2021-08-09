#
#  Copyright (c) 2019-2020 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

cmake_minimum_required(VERSION 3.14)

project(JammerNetz)

# For the digital stage branded build, provide an option
OPTION(DIGITAL_STAGE_BRANDING OFF)

# Since we also build MacOS, we need C++ 17. Which is not a bad thing.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(APPLE)
add_compile_options(-Wno-unused-command-line-argument)
ENDIF()

# To avoid dependency on WebKit. This also came with MacOS, but as webkit is heavyweight, it is probably a good idea to turn it off for all
option(JUCE_CONFIG_JUCE_WEB_BROWSER OFF) 

# Allow to turn on crash reporting via Sentry. This is turned on by default for the builds I upload to github
OPTION(SENTRY_CRASH_REPORTING "Turn on crash reporting via Internet/Sentry")
set(SENTRY_DSN "Sentry DSN URL" CACHE STRING "https://YYYYYYYYYYYYYYYYYY@ZZZZZ.ingest.sentry.io/XXXX")

IF(SENTRY_CRASH_REPORTING)
	message("Internet-based crash reporting is turned on, using Sentry endpoint authorization from SENTRY_DSN CMake variable")
	SET(SENTRY_INSTALL_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/sentry-native/install")
	SET(SENTRY_DISTRIBUTION_FILES "${SENTRY_INSTALL_PATH}/bin/sentry.dll" "${SENTRY_INSTALL_PATH}/bin/crashpad_handler.exe")
	set(SENTRY_INCLUDE_DIR "${SENTRY_INSTALL_PATH}/include")
	message(STATUS "Setting SENTRY_INSTALL_PATH to ${SENTRY_INSTALL_PATH}")
ENDIF()

# We need a little bit support for the JUCE library, and also want to put our own CMake helpers on the module path
SET(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Include useful scripts for CMake
include(FetchContent)

# Configure time downloads of more third_party libraries that can not be maintained as git submodules
# Make sure to read and accept the Steinberg licensing agreement in the directory _deps/asiosdk-src when you build the Windows version!
if (WIN32)
	FetchContent_Declare(
	  asiosdk
	  URL https://www.steinberg.net/asiosdk
	)
	FetchContent_MakeAvailable(asiosdk)
	set(ASIO_SDK_DIRECTORY "${asiosdk_SOURCE_DIR}/common")

	FetchContent_Declare(
		vcredist
		URL https://aka.ms/vs/16/release/vc_redist.x64.exe
		DOWNLOAD_NO_EXTRACT true
	)
	FetchContent_MakeAvailable(vcredist)
	set(VCREDIST_PATH "${CMAKE_CURRENT_BINARY_DIR}/_deps/vcredist-subbuild/vcredist-populate-prefix/src")
elseif(APPLE)
	# Supress a warning from juce_TargetPlatform by letting it know the build type
	# NOTE that NDEBUG is set automatically in Release
	if (NOT CMAKE_BUILD_TYPE MATCHES Release)
		add_compile_definitions(DEBUG)
	endif()
elseif(UNIX)
    # This else branch is Linux, then.
	ADD_DEFINITIONS(-DJUCE_JACK)

	# Include useful scripts for CMake
	find_package(PkgConfig REQUIRED) 
	find_package(OpenGL)

	# These calls create special `PkgConfig::<MODULE>` variables
	pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
	pkg_check_modules(GLEW REQUIRED IMPORTED_TARGET glew)
	pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.0)  # TODO Test that this is really no longer required
endif()


# We need a cryptographic key that server and client will share so they can communicate. In its simplest form, we will just take some random numbers from the internet
set(CRYPTO_FILE "${CMAKE_CURRENT_LIST_DIR}/RandomNumbers.bin")
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

enable_testing()
include(GoogleTest)    
set(gtest_force_shared_crt ON) # needed to build on Windows
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # otherwise the flag set above is ignored?
add_subdirectory(third_party/googletest)

# See https://cmake.org/cmake/help/v3.10/module/GoogleTest.html
# Keep cache clean
mark_as_advanced(
    BUILD_GMOCK BUILD_GTEST BUILD_SHARED_LIBS
    gmock_build_tests gtest_build_samples gtest_build_tests
    gtest_disable_pthreads gtest_force_shared_crt gtest_hide_internal_symbols
)

# Define the list of link libraries required on Linux linking with JUCE, this must be used by any executable / module to run standalone
if(UNIX AND NOT APPLE)
	set(LINUX_JUCE_LINK_LIBRARIES
		PkgConfig::WEBKIT 
		PkgConfig::GTK 
		PkgConfig::GLEW 
		Xext 
		X11 
		pthread 
		${CMAKE_DL_LIBS} 
		freetype 
		curl 
		asound)

	# Also, as we will be building a shared module for Python, make sure GCC generates relocatable code suited for inclusion in a shared library
	add_definitions(-fPIC)
ENDIF()

# Adding JUCE 6.0
add_subdirectory("third_party/JUCE")

# Build a static library from juce 
add_library(juce-static STATIC)
target_link_libraries(juce-static
	PRIVATE
	    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
	    juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
	    juce::juce_cryptography juce::juce_data_structures juce::juce_dsp
	    juce::juce_events juce::juce_graphics juce::juce_gui_basics
	    juce::juce_gui_extra juce::juce_opengl juce::juce_video
		${LINUX_JUCE_LINK_LIBRARIES}
	PUBLIC
		juce::juce_recommended_config_flags
		juce::juce_recommended_lto_flags
		juce::juce_recommended_warning_flags)

target_compile_definitions(juce-static
	PUBLIC
		JUCE_PLUGINHOST_VST=0
		JUCE_PLUGINHOST_AU=0
		DONT_SET_USING_JUCE_NAMESPACE=1
		JUCE_DISPLAY_SPLASH_SCREEN=0
		JUCE_REPORT_APP_USAGE=0
		JUCE_CHECK_MEMORY_LEAKS=0
		JUCE_QUICKTIME=0
		JUCE_USE_DIRECTWRITE=1
		JUCE_ASIO=1
		#JUCE_DIRECT2D=1  # That doesn't work, and the JUCE forum is full of confusing threads about Direct2D and font rendering
		JUCE_CATCH_UNHANDLED_EXCEPTIONS=0
		JUCE_COREGRAPHICS_DRAW_ASYNC=1
		JUCE_WIN_PER_MONITOR_DPI_AWARE=1
		JUCE_USE_FLAC=1
		# JUCE_USE_WINRT_MIDI=1 # So much doesn't work when activating this that this is really scary
	INTERFACE
		$<TARGET_PROPERTY:juce-static,COMPILE_DEFINITIONS>)

target_include_directories(juce-static
	INTERFACE
		$<TARGET_PROPERTY:juce-static,INCLUDE_DIRECTORIES>
if (WIN32)
	PRIVATE
		"${ASIO_SDK_DIRECTORY}"
endif()
		)

set_target_properties(juce-static PROPERTIES
	POSITION_INDEPENDENT_CODE TRUE
	VISIBILITY_INLINES_HIDDEN TRUE
	C_VISIBILITY_PRESET hidden
	CXX_VISIBILITY_PRESET hidden)


# Configure TBB
set(TBB_TEST OFF)
if(WIN32)
	string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
	list(GET VERSION_LIST 0 MSVC_VERSION_MAJOR)
	list(GET VERSION_LIST 1 MSVC_VERSION_MINOR)
	SET(MY_TBB_BINARY_DIR_BASE msvc_${MSVC_VERSION_MAJOR}.${MSVC_VERSION_MINOR}_cxx17_64_md)
	message("Determined TBB binary directory base as ${MY_TBB_BINARY_DIR_BASE}")
endif()

# Add subdirectories
add_subdirectory("third_party/JUCE-user-modules")
add_subdirectory("modules/juce-utils")
message(${CMAKE_MODULE_PATH})
add_subdirectory("modules/libds")
add_subdirectory("lookAndFeel")
add_subdirectory("third_party/oneTBB")
add_subdirectory("common")
add_subdirectory("Server")
add_subdirectory("Client")

# The client installer now contains the server as well for local testing - make sure to always build the server
add_dependencies(JammerNetzClient JammerNetzServer)