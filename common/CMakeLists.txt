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


# Load the macro to process binary resources
include(BinaryResources)

# Add an include for a library that is not included via CMake itself
# Adding the include path for the ff_meters JUCE module. As juce-cmake doesn't support modules at user locations, we need to be a bit creative here
include_directories(AFTER "../third_party/tbb2019_20190605oss/include/")

# Call the Cap'n Proto interface generator
set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR})
capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS JammerNetzPackages.capnp)

# Create our binary resources
create_resources(resources BinaryResources.h)

# Define the sources for the static library
set(Sources
	BuffersConfig.h
	CMakeLists.txt
	JammerNetzPackage.cpp
	JammerNetzPackage.h	
	${CAPNP_SRCS}
	${CAPNP_HDRS}
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
target_link_libraries(JammerCommon PRIVATE ${JUCE_LIBRARIES} CapnProto::capnp)
