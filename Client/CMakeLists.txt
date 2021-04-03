#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzClient" VERSION 2.1.0)

configure_file("Source/version.cpp.in" "Source/version.cpp")

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
	Source/PlayalongDisplay.cpp Source/PlayalongDisplay.h
	Source/Resources.h
	Source/version.cpp.in
)
source_group(Source FILES ${SOURCES})

set(INSTALLER_FILES
	setup_client.iss
	info.plist
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
	Source/ChannelControllerGroup.cpp
	Source/ChannelControllerGroup.h
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


add_executable(JammerNetzClient MACOSX_BUNDLE WIN32 ${SOURCES} ${UICOMPONENT_FILES} ${AUDIO_PROCESSING_FILES} ${MIDI_FILES} ${RESOURCE_FILES} ${INSTALLER_FILES})
target_include_directories(JammerNetzClient PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Source")
IF(WIN32)
	target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} JammerCommon juce-utils ff_meters)
elseif(APPLE)
	target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} JammerCommon juce-utils ff_meters tbb)
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
	# Post build commands - copy the DLLs (Windows only I guess). Due to the directory format of the oneTBB output directory, we need to parse the MSVC compiler version and use it appropriately
	add_custom_command(
			TARGET JammerNetzClient POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
					${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Release>,${MY_TBB_BINARY_DIR_BASE}_release/tbb12.dll,${MY_TBB_BINARY_DIR_BASE}_debug/tbb12_debug.dll>
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

IF(APPLE)
	# For apple, set the properties file to be included in the link. This is important to allow Microphone access on macOS (you will get a dialog asking for permission)
	SET_TARGET_PROPERTIES(JammerNetzClient PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "JammerNetz ${JammerNetzClient_VERSION}"
		#MACOSX_BUNDLE_ICON_FILE icon_orm.icns
		MACOSX_BUNDLE_BUNDLE_VERSION ${JammerNetzClient_VERSION}
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
		)
ENDIF()

IF(APPLE)
    # This is supposed to build a relocatable macOS DMG installer when you specify the
    # --target package
    SET(MY_RELEASE_DIR ${CMAKE_BINARY_DIR}/macInstaller)
    SET(APPS "\${CMAKE_INSTALL_PREFIX}/JammerNetzClient.app")
    SET(DIRS "${CMAKE_BINARY_DIR}")
    INSTALL(TARGETS JammerNetzClient
        BUNDLE DESTINATION . COMPONENT Runtime
        RUNTIME DESTINATION bin COMPONENT Runtime
        )
    
    INSTALL(CODE "include(BundleUtilities)
	set(BU_CHMOD_BUNDLE_ITEMS TRUE)
    fixup_bundle(\"${APPS}\"  \"\"  \"${DIRS}\" IGNORE_ITEM \"Python\")
    " COMPONENT Runtime)
    
    # Setup CPack variables
    #set(CPACK_PACKAGE_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_orm.png)
    set(CPACK_PACKAGE_VERSION ${JammerNetzClient_VERSION})
    set(CPACK_PACKAGE_VENDOR "Christof Ruch Beratungs UG (haftungsbeschraenkt)")
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/Redist/agpl-3.0.txt)
    
    # Select only the dmg generator for now
    SET(CPACK_BINARY_DRAGNDROP ON)
    SET(CPACK_BINARY_TGZ OFF)
    SET(CPACK_BINARY_STGZ OFF)
    INCLUDE(CPack)
ENDIF()
