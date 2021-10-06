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
IF (WIN32)
	set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/builds/$<CONFIG>/flatc.exe")
ELSE()
    set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/LinuxBuilds/flatc")
ENDIF()
FLATBUFFERS_GENERATE_C_HEADERS(FlatJammer JammerNetzPackages.fbs JammerNetzAudioData.fbs)

# Define the sources for the static library
set(Sources
	BuffersConfig.h
	CMakeLists.txt
	Encryption.cpp Encryption.h
	JammerNetzClientInfoMessage.cpp JammerNetzClientInfoMessage.h
	JammerNetzPackage.cpp JammerNetzPackage.h
	JammerNetzPackages.fbs
	JammerNetzAudioData.fbs
	${FlatJammer_OUTPUTS}
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
target_include_directories(JammerCommon
	PUBLIC "${INTEL_TBB_DIRECTORY}/include/" "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/include" "${CMAKE_CURRENT_BINARY_DIR}"
	INTERFACE ${CMAKE_CURRENT_LIST_DIR}
	PRIVATE ${JUCE_INCLUDES}
)
target_link_libraries(JammerCommon PUBLIC TBB::tbb PRIVATE juce-utils)

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerCommon PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(JammerCommon PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

# Define tests
set(TESTNAME CommonTest)
add_executable(${TESTNAME} JammerCommonTests.cpp)
target_link_libraries(${TESTNAME} gtest gmock gtest_main juce-utils JammerCommon ${JUCE_LIBRARIES})
gtest_discover_tests(${TESTNAME}
    WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY}
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}"
)
set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
