#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzServer" VERSION 2.0.0)

include_directories("${INTEL_TBB_DIRECTORY}/include/")

configure_file("Source/version.cpp.in" "Source/version.cpp")

set(SOURCES
	Source/Main.cpp
	Source/AcceptThread.cpp Source/AcceptThread.h
	Source/MixerThread.cpp Source/MixerThread.h
	Source/SendThread.cpp Source/SendThread.h
	Source/SharedServerTypes.h
	Source/version.cpp.in
)

add_executable(JammerNetzServer ${SOURCES})
target_include_directories(JammerNetzServer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Source")
if (WIN32)
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon)

	# Post build commands - copy the DLLs
	add_custom_command(
			TARGET JammerNetzServer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
					${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Release>,msvc_19.16_cxx17_64_md_release/tbb12.dll,msvc_19.16_cxx17_64_md_debug/tbb12_debug.dll>
					${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
elseif(APPLE)
	target_link_directories(JammerNetzServer PRIVATE "${INTEL_TBB_DIRECTORY}/lib/")
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon tbb)
else()
	target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} juce-utils JammerCommon tbb ${CMAKE_DL_LIBS} pthread asound curl)
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