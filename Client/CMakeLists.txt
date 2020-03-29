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
include_directories("${INTEL_TBB_DIRECTORY}/include/"
	"${asiosdk_SOURCE_DIR}/common"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party/infra/include"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party/Q/q_lib/include"
	"${CMAKE_CURRENT_LIST_DIR}/../third_party")

# Create our binary resources
create_resources(Resources Source/Resources.h)

# Setup definition for ASIO support
add_definitions(-DJUCE_ASIO)

set(SOURCES
	Source/AudioDeviceDiscovery.cpp
	Source/AudioDeviceDiscovery.h
	Source/LayoutConstants.h
	Source/Main.cpp
	Source/MainComponent.cpp
	Source/MainComponent.h
	Source/MidiPlayAlong.cpp Source/MidiPlayAlong.h
	Source/PlayAlongDisplay.cpp Source/PlayAlongDisplay.h
	Source/Resources.h
)
source_group(Source FILES ${SOURCES})

set(INSTALLER_FILES
	setup_client.iss
	Redist/agpl-3.0.txt
)
source_group(Source\\Installer FILES ${INSTALLER_FILES})

set(RESOURCE_FILES
	Resources/about.txt
	Resources/cloud.png
	Resources/live.png
)
source_group(Source\\Resources FILES ${RESOURCE_FILES})

set(AUDIO_PROCESSING_FILES
	Source/AudioCallback.cpp
	Source/AudioCallback.h
	Source/Tuner.cpp
	Source/Tuner.h
	Source/Client.cpp
	Source/Client.h
	Source/DataReceiveThread.cpp
	Source/DataReceiveThread.h
)
source_group(Source\\AudioProcessing FILES ${AUDIO_PROCESSING_FILES})

set(UICOMPONENT_FILES
	Source/BPMDisplay.cpp
	Source/BPMDisplay.h
	Source/ChannelController.cpp
	Source/ChannelController.h
	Source/ClientConfig.cpp
	Source/ClientConfig.h
	Source/DeviceSelector.cpp
	Source/DeviceSelector.h
	Source/RecordingInfo.cpp
	Source/RecordingInfo.h
	Source/ServerSelector.cpp
	Source/ServerSelector.h
	Source/ServerStatus.cpp
	Source/ServerStatus.h
)
source_group(Source\\UIComponents FILES ${UICOMPONENT_FILES})


add_executable(JammerNetzClient WIN32 ${SOURCES} ${UICOMPONENT_FILES} ${AUDIO_PROCESSING_FILES} ${MIDI_FILES} ${RESOURCE_FILES} ${INSTALLER_FILES})

IF(WIN32)
	target_link_directories(JammerNetzClient PRIVATE "${INTEL_TBB_DIRECTORY}/lib/intel64/vc14")
	target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} JammerCommon juce-utils ff_meters)
ELSE()
	target_link_libraries(JammerNetzClient 
		${JUCE_LIBRARIES} 
		PkgConfig::WEBKIT 
		PkgConfig::GTK 		
		PkgConfig::GLEW 		
		Xext 
		X11 
		pthread 
		${CMAKE_DL_LIBS} 
		freetype 
		curl 
		asound 
		tbb
		JammerCommon juce-utils ff_meters)
ENDIF()

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerNetzClient PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(JammerNetzClient PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

IF(WIN32)
	# Post build commands - copy the DLLs (Windows only I guess)
	add_custom_command(
			TARGET JammerNetzClient POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
					${INTEL_TBB_DIRECTORY}/bin/intel64/vc14/$<IF:$<CONFIG:Release>,tbb,tbb_debug>.dll
					${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
ENDIF()

IF(WIN32)
	# Windows only: Build InnoSetup installer
	find_program(INNOSETUP NAMES iscc ISCC HINTS "c:/program files (x86)/Inno Setup 6")
	if (INNOSETUP)
	else()
		message(WARNING "Didn't find InnoSetup 6 installation, skipping creation of Client installer '")
	endif()
ENDIF()

IF(WIN32)
	 # Parse Inno Setup config file
	configure_file("${CMAKE_CURRENT_LIST_DIR}/setup_client.iss"
				   "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss")
	set(no_copy $<NOT:$<CONFIG:Release>>)
	add_custom_command(
		TARGET JammerNetzClient POST_BUILD
		COMMAND  "${CMAKE_COMMAND}" -E $<${no_copy}:echo> $<${no_copy}:"Not building Installer with "> chdir . ${INNOSETUP} "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss"
		DEPENDS setup_client.iss 
	)
ENDIF()

