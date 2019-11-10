#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzServer")

# Find JUCE, and add the components we need
find_package(JUCE REQUIRED 
	COMPONENTS 
		juce_core		
		juce_events
		juce_audio_basics
		juce_audio_devices
		juce_audio_formats
		juce_data_structures
		juce_cryptography
)

include_directories("${INTEL_TBB_DIRECTORY}/include/")


set(SOURCES
	Source/Main.cpp
	Source/AcceptThread.cpp Source/AcceptThread.h
	Source/MixerThread.cpp Source/MixerThread.h
	Source/SendThread.cpp Source/SendThread.h
	Source/SharedServerTypes.h
)

add_executable(JammerNetzServer ${SOURCES})
target_link_directories(JammerNetzServer PRIVATE "${INTEL_TBB_DIRECTORY}/lib/intel64/vc14")
target_link_libraries(JammerNetzServer  ${JUCE_LIBRARIES} JammerCommon)
source_group(Source FILES ${SOURCES})

# Post build commands - copy the DLLs (Windows only I guess)
add_custom_command(
        TARGET JammerNetzServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${INTEL_TBB_DIRECTORY}/bin/intel64/vc14/$<IF:$<CONFIG:Release>,tbb,tbb_debug>.dll
                ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
