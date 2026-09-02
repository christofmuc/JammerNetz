#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

# Configure Sentry
IF(SENTRY_CRASH_REPORTING)
	configure_file("${CMAKE_CURRENT_LIST_DIR}/sentry-config.h.in" "${CMAKE_CURRENT_LIST_DIR}/sentry-config.h")
ENDIF()

# Add flatbuffers
find_package(Flatbuffers REQUIRED PATHS "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers")
include(HomegrownFlatC)

# Compile Flatbuffers. The output will be found because we added CMAKE_CURRENT_BINARY_DIR to the list of includes above
IF (NOT FLATBUFFERS_FLATC_EXECUTABLE)
	IF (WIN32)
		if (CMAKE_CONFIGURATION_TYPES)
			set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/Builds/$<CONFIG>/flatc.exe")
		else()
			set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/Builds/flatc.exe")
		endif()
	ELSE()
		set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/LinuxBuilds/flatc")
	ENDIF()
ENDIF()
set(FLATBUFFER_INPUT
		JammerNetzPackages.fbs
		JammerNetzAudioData.fbs
		JammerNetzChannelSetup.fbs
		JammerNetzSessionInfo.fbs
		JammerNetzControlMessage.fbs
		)
FLATBUFFERS_GENERATE_C_HEADERS(FlatJammer ${FLATBUFFER_INPUT})

# Define the sources for the static library
set(Sources
	BuffersConfig.h
	CMakeLists.txt
	Encryption.cpp Encryption.h
	JammerNetzClientInfoMessage.cpp JammerNetzClientInfoMessage.h
	JammerNetzPackage.cpp JammerNetzPackage.h
	JuceHeader.h
	${FLATBUFFER_INPUT}
	PacketStreamQueue.cpp PacketStreamQueue.h
	Pool.h
	Recorder.cpp Recorder.h
	RingOfAudioBuffers.h
	RunningStats.cpp RunningStats.h
	sentry-config.h.in
	ServerInfo.cpp ServerInfo.h
	XPlatformUtils.h
)
# Setup library
add_library(JammerCommon ${Sources})
foreach(OPT ${FlatJammer_OUTPUTS})
	message("Adding source file ${OPT}")
target_sources(JammerCommon PRIVATE ${OPT})
endforeach()

target_include_directories(JammerCommon
	PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/include" "${CMAKE_CURRENT_BINARY_DIR}"
	INTERFACE ${CMAKE_CURRENT_LIST_DIR}
	PRIVATE ${JUCE_INCLUDES}
)
target_link_libraries(JammerCommon PRIVATE juce-utils)

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerCommon PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    target_compile_options(JammerCommon PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

# Define tests
set(TESTNAME CommonTest)
add_executable(${TESTNAME} JammerCommonTests.cpp)
target_link_libraries(${TESTNAME} gtest gmock gtest_main juce-utils JammerCommon ${JUCE_LIBRARIES})
jammernetz_copy_msvc_debug_runtime(${TESTNAME})
gtest_add_tests(TARGET ${TESTNAME}
	SOURCES JammerCommonTests.cpp
	TEST_LIST COMMON_UNIT_TESTS
)
set_tests_properties(${COMMON_UNIT_TESTS} PROPERTIES LABELS unit TIMEOUT 30)
set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
