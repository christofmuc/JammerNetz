#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

# Find JUCE, and add the components we need
IF(NOT DEFINED JUCE_LIBRARIES)
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
ENDIF()

# Add flatbuffers
find_package(Flatbuffers REQUIRED PATHS "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers")
include(HomegrownFlatC)

# Load the macro to process binary resources
include(BinaryResources)

# Create our binary resources
create_resources(resources BinaryResources.h)

# Compile Flatbuffers. The output will be found because we added CMAKE_CURRENT_BINARY_DIR to the list of includes above
IF (WIN32)	
	set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/builds/Debug/flatc.exe")
ELSE()	
    set(FLATBUFFERS_FLATC_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/LinuxBuilds/flatc")
ENDIF()
FLATBUFFERS_GENERATE_C_HEADERS(FlatJammer JammerNetzPackages.fbs JammerNetzAudioData.fbs)

# Define the sources for the static library
set(Sources
	BuffersConfig.h
	CMakeLists.txt
	JammerNetzClientInfoMessage.cpp JammerNetzClientInfoMessage.h
	JammerNetzPackage.cpp JammerNetzPackage.h	
	JammerNetzPackages.fbs
	JammerNetzAudioData.fbs
	${FlatJammer_OUTPUTS}
	PacketStreamQueue.cpp PacketStreamQueue.h
	BinaryResources.h
	Recorder.cpp Recorder.h
	RingOfAudioBuffers.h
	ServerInfo.cpp ServerInfo.h
)

# Setup library
add_library(JammerCommon ${Sources})
target_include_directories(JammerCommon 
	PUBLIC "${INTEL_TBB_DIRECTORY}/include/" "${CMAKE_CURRENT_LIST_DIR}/../third_party/flatbuffers/include" "${CMAKE_CURRENT_BINARY_DIR}"
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
    #target_compile_options(JammerCommon PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
