#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzServer" VERSION 2.4.1)

configure_file("Source/version.cpp.in" "Source/version.cpp")

if(WIN32)
	# Need PDCurses for nicer statistics output
	find_package(pdcurses REQUIRED PATHS ${CMAKE_BINARY_DIR} NO_DEFAULT_PATH)
endif()

set(SOURCES
	Source/Main.cpp
	Source/ClientState.cpp Source/ClientState.h
	Source/AcceptThread.cpp Source/AcceptThread.h
	Source/MixerThread.cpp Source/MixerThread.h
	Source/SendThread.cpp Source/SendThread.h
	Source/SharedServerTypes.h
	Source/ServerLogger.cpp Source/ServerLogger.h
	Source/ServerPort.h
	Source/version.cpp.in
)

add_executable(JammerNetzServer ${SOURCES})
target_include_directories(JammerNetzServer PRIVATE "${SENTRY_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/Source")
if(SENTRY_CRASH_REPORTING)
	target_compile_options(JammerNetzServer PRIVATE -DUSE_SENTRY)
	set(SENTRY_LIB sentry)
	message(STATUS "Configuring server to link with ${SENTRY_INSTALL_PATH}/lib")
	target_link_directories(JammerNetzServer PRIVATE "${SENTRY_INSTALL_PATH}/lib")
endif()

add_executable(ClientStateTest Source/ClientStateTests.cpp Source/ClientState.cpp Source/ClientState.h Source/SharedServerTypes.h)
target_include_directories(ClientStateTest PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Source")
target_link_libraries(ClientStateTest PRIVATE gtest gmock gtest_main juce-utils JammerCommon TBB::tbb ${JUCE_LIBRARIES})
jammernetz_copy_msvc_debug_runtime(ClientStateTest)
gtest_add_tests(TARGET ClientStateTest SOURCES Source/ClientStateTests.cpp)
set_target_properties(ClientStateTest PROPERTIES FOLDER tests)

set(TESTNAME ServerTest)
add_executable(${TESTNAME} Source/ServerPortTests.cpp)
target_link_libraries(${TESTNAME} PRIVATE gtest_main)
jammernetz_copy_msvc_debug_runtime(${TESTNAME})
gtest_add_tests(TARGET ${TESTNAME} SOURCES Source/ServerPortTests.cpp)
set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
target_link_libraries(JammerNetzServer PRIVATE ${JUCE_LIBRARIES} juce-utils JammerCommon TBB::tbb ${SENTRY_LIB})
if (WIN32)
	target_link_libraries(JammerNetzServer PRIVATE pdcurses::pdcurses)

	# Post build commands - copy the DLLs
	add_custom_command(
			TARGET JammerNetzServer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
					${SENTRY_DISTRIBUTION_FILES}
					${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Release>,${MY_TBB_BINARY_DIR_BASE}_release/tbb12.dll,$<IF:$<CONFIG:Debug>,${MY_TBB_BINARY_DIR_BASE}_debug/tbb12_debug.dll,${MY_TBB_BINARY_DIR_BASE}_relwithdebinfo/tbb12.dll>>
					$<TARGET_FILE_DIR:JammerNetzServer>)
	jammernetz_copy_msvc_debug_runtime(JammerNetzServer)
elseif(APPLE)
	target_link_libraries(JammerNetzServer PRIVATE ncurses)
else()
	target_link_libraries(JammerNetzServer PRIVATE ${CMAKE_DL_LIBS} pthread asound curl ncurses)
endif()


source_group(Source FILES ${SOURCES})

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerNetzServer PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    target_compile_options(JammerNetzServer PRIVATE -Wall -Wextra -pedantic -Werror)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        # oneTBB triggers redundant-decls on GCC ARM in system headers; keep warnings on, but do not fail the build on this one.
        target_compile_options(JammerNetzServer PRIVATE -Wno-error=redundant-decls)
    endif()
endif()
