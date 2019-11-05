#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzClient")

# Find JUCE, and add the components we need
find_package(JUCE REQUIRED 
	COMPONENTS 
		juce_core		
		juce_events
		juce_audio_basics
		juce_audio_devices
		juce_audio_formats
		juce_audio_processors
		juce_audio_utils
		juce_data_structures
		juce_cryptography
		juce_dsp
		juce_gui_basics
		juce_gui_extra
		juce_graphics
		juce_opengl
)

# Load the macro to process binary resources
include(BinaryResources)

# Setup some additional include directories for our header only libraries and those without CMake support
include_directories("${CMAKE_CURRENT_LIST_DIR}/../third_party/tbb2019_20190605oss/include/"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party/ASIOSDK/common"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party/infra/include"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party/Q/q_lib/include"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party")

# Create our binary resources
create_resources(Resources Source/Resources.h)

# Setup definition for ASIO support
add_definitions(-DJUCE_ASIO)

set(SOURCES
	Source/AudioCallback.cpp
	Source/AudioCallback.h
	Source/AudioDeviceDiscovery.cpp
	Source/AudioDeviceDiscovery.h
	Source/BPMDisplay.cpp
	Source/BPMDisplay.h
	Source/ChannelController.cpp
	Source/ChannelController.h
	Source/Client.cpp
	Source/Client.h
	Source/ClientConfig.cpp
	Source/ClientConfig.h
	Source/Data.cpp
	Source/Data.h
	Source/DataReceiveThread.cpp
	Source/DataReceiveThread.h
	Source/DeviceSelector.cpp
	Source/DeviceSelector.h
	Source/LayoutConstants.h
	Source/Main.cpp
	Source/MainComponent.cpp
	Source/MainComponent.h
	Source/MidiClocker.cpp
	Source/MidiClocker.h
	Source/MidiNote.cpp
	Source/MidiNote.h
	Source/MidiRecorder.cpp
	Source/MidiRecorder.h
	Source/RecordingInfo.cpp
	Source/RecordingInfo.h
	Source/ServerSelector.cpp
	Source/ServerSelector.h
	Source/ServerStatus.cpp
	Source/ServerStatus.h
	Source/Tuner.cpp
	Source/Tuner.h
	Source/Resources.h
)

add_executable(JammerNetzClient WIN32 ${SOURCES})
target_link_directories(JammerNetzClient PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../third_party/tbb2019_20190605oss/lib/intel64/vc14")
target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} JammerCommon ff_meters)
source_group(Source FILES ${SOURCES})

# Post build commands - copy the DLLs (Windows only I guess)
add_custom_command(
        TARGET JammerNetzClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_LIST_DIR}/../third_party/tbb2019_20190605oss/bin/intel64/vc14/tbb_debug.dll
                ${CMAKE_CURRENT_BINARY_DIR})
