#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzServer" VERSION 2.1.0)

configure_file("Source/version.cpp.in" "Source/version.cpp")

find_package(TBB)

if(WIN32)
	# Need PDCurses for nicer statistics output
	set(CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")
	message(STATUS "CMAKE module path is ${CMAKE_MODULE_PATH}")
	find_package(pdcurses REQUIRED)
endif()

set(SOURCES
	Source/Main.cpp
	Source/AcceptThread.cpp Source/AcceptThread.h
	Source/MixerThread.cpp Source/MixerThread.h
	Source/SendThread.cpp Source/SendThread.h
	Source/SharedServerTypes.h
	Source/ServerLogger.cpp Source/ServerLogger.h
	Source/version.cpp.in
)

add_executable(JammerNetzServer ${SOURCES})
target_include_directories(JammerNetzServer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Source")
if (WIN32)
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon pdcurses::pdcurses tbb$<$<CONFIG:Debug>:_debug>)

	# Post build commands - copy the DLLs
	# The DLLs have been placed in their position in CMAKE_BINARY_DIR by a previous conan install -if ...... command, if all went well
	add_custom_command(
			TARGET JammerNetzServer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
					${CMAKE_BINARY_DIR}/tbb$<$<CONFIG:Debug>:_debug>.dll
					${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
elseif(APPLE)
	target_link_directories(JammerNetzServer PRIVATE "${INTEL_TBB_DIRECTORY}/lib/")
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon tbb ncurses)
else()
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon tbb ${CMAKE_DL_LIBS} pthread asound curl ncurses)
endif()
source_group(Source FILES ${SOURCES})

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerNetzServer PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(JammerNetzServer PRIVATE -Wall -Wextra -pedantic -Werror)
endif()