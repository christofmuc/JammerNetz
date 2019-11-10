#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

# Find JUCE, and add the components we need
SET(JUCE_DIR_HINT "${CMAKE_CURRENT_LIST_DIR}/../thirdparty")
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

# Add flatbuffers
#add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers"
#                 ${CMAKE_CURRENT_BINARY_DIR}/flatbuffers-build
#                 EXCLUDE_FROM_ALL)

# Load the macro to process binary resources
include(BinaryResources)

# Add an include for a library that is not included via CMake itself
include_directories(AFTER "${INTEL_TBB_DIRECTORY}/include/" "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/include")

# Create our binary resources
create_resources(resources BinaryResources.h)

# Define the sources for the static library
set(Sources
	BuffersConfig.h
	CMakeLists.txt
	JammerNetzPackage.cpp
	JammerNetzPackage.h	
	JammerNetzPackages.fbs
	JammerNetzPackages_generated.h
	PacketStreamQueue.cpp
	PacketStreamQueue.h
	BinaryResources.h
	Recorder.cpp
	Recorder.h
	RingOfAudioBuffers.h
	ServerInfo.cpp
	ServerInfo.h
	Settings.cpp
	Settings.h
	StreamLogger.cpp
	StreamLogger.h
)

# Setup library
add_library(JammerCommon ${Sources})
target_include_directories(JammerCommon INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(JammerCommon PRIVATE ${JUCE_LIBRARIES})
