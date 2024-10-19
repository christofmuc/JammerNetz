#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzClient" VERSION 2.3.0)

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
if(WIN32)
add_definitions(-DJUCE_ASIO)
endif()

set(SOURCES
	Source/ApplicationState.cpp Source/ApplicationState.h
	Source/AudioDeviceDiscovery.cpp
	Source/AudioDeviceDiscovery.h
	Source/LayoutConstants.h
	Source/Main.cpp
	Source/MainComponent.cpp
	Source/MainComponent.h
	Source/Resources.h
	Source/version.cpp.in
)
source_group(Source FILES ${SOURCES})

set(INSTALLER_FILES
	setup_client.iss.in
	info.plist
	Redist/agpl-3.0.txt
)
source_group(Source\\Installer FILES ${INSTALLER_FILES})

set(RESOURCE_FILES
	Resources/JammerNetzIcon.icns
	Resources/about.txt
	Resources/cloud.png
	Resources/live.png
)
source_group(Source\\Resources FILES ${RESOURCE_FILES})

set(AUDIO_PROCESSING_FILES
	Source/AudioCallback.cpp
	Source/AudioCallback.h
	Source/AudioService.cpp
	Source/AudioService.h
	Source/MidiSendThread.cpp Source/MidiSendThread.h
	Source/Tuner.cpp
	Source/Tuner.h
)
source_group(Source\\AudioProcessing FILES ${AUDIO_PROCESSING_FILES})

set(NETWORK_FILES
	Source/JammerService.cpp
	Source/JammerService.h
	Source/Client.cpp
	Source/Client.h
	Source/DataReceiveThread.cpp
	Source/DataReceiveThread.h
)
source_group(Source\\Networking FILES ${NETWORK_FILES})

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
	Source/MidiDeviceSelector.cpp
	Source/MidiDeviceSelector.h
	Source/RecordingInfo.cpp
	Source/RecordingInfo.h
	Source/ServerSelector.cpp
	Source/ServerSelector.h
	Source/ServerStatus.cpp
	Source/ServerStatus.h
)
source_group(Source\\UIComponents FILES ${UICOMPONENT_FILES})

set(PLAYALONG_FILES
	Source/MidiPlayAlong.cpp Source/MidiPlayAlong.h
	Source/PlayalongDisplay.cpp Source/PlayalongDisplay.h
)
source_group(Source\\PlayAlong FILES ${PLAYALONG_FILES})

set(DSBRANCH_FILES
	Source/DataStore.cpp Source/DataStore.h
	Source/DSConfig.h
	Source/JoinStageDialog.cpp Source/JoinStageDialog.h
	Source/Login.cpp Source/Login.h
)
source_group(Source\\DigitalStage FILES ${DSBRANCH_FILES})

set(EXECUTABLE_BASE_NAME "JammerNetzClient")
set(INSTALLER_BASE_NAME "JammerNetz")

add_executable(JammerNetzClient MACOSX_BUNDLE WIN32 ${SOURCES} ${DS_SOURCES} ${UICOMPONENT_FILES} ${PLAYALONG_FILES} ${AUDIO_PROCESSING_FILES} ${NETWORK_FILES} ${MIDI_FILES} ${RESOURCE_FILES} ${INSTALLER_FILES})
set(JammerNetz_VersionedName "JammerNetz 2")
set(JammerNetz_ApplicationName "JammerNetzClient")
set(JammerNetz_AppId "{{704C397B-E756-47B0-AB7A-5C52A62E0A3F}")
set(JammerNetz_InstallerBaseName "jammernetz_setup")

message("Crash reporting: ${SENTRY_CRASH_REPORTING} at ${SENTRY_INSTALL_PATH}")
if(SENTRY_CRASH_REPORTING)
	target_compile_options(JammerNetzClient PRIVATE -DUSE_SENTRY)
	set(SENTRY_LIB sentry)
	target_link_directories(JammerNetzClient PRIVATE "${SENTRY_INSTALL_PATH}/lib")
endif()

target_include_directories(JammerNetzClient PRIVATE "${SENTRY_INCLUDE_DIR}"  "${CMAKE_CURRENT_BINARY_DIR}/Source")
IF(WIN32)
	target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} ${DS_LIBRARIES} JammerCommon juce-utils juce-widgets midikraft-base ff_meters ${SENTRY_LIB})
elseif(APPLE)
	target_link_libraries(JammerNetzClient ${JUCE_LIBRARIES} ${DS_LIBRARIES} JammerCommon juce-utils juce-widgets midikraft-base ff_meters tbb)
	# ${SENTRY_LIB})
ELSE()
	target_link_libraries(JammerNetzClient
		${JUCE_LIBRARIES}
		${DS_LIBRARIES}
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
		${SENTRY_LIB}
		JammerCommon juce-utils juce-widgets midikraft-base ff_meters)
ENDIF()

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerNetzClient PRIVATE /W4 /WX)
else()
    # lots of warnings and all warnings as errors
    #target_compile_options(JammerNetzClient PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(JammerNetzClient PRIVATE -Wno-unknown-pragmas)
endif()

IF(WIN32)
	# Post build commands - copy the DLLs (Windows only I guess). Due to the directory format of the oneTBB output directory, we need to parse the MSVC compiler version and use it appropriately
	add_custom_command(
			TARGET JammerNetzClient POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
					${SENTRY_DISTRIBUTION_FILES}
					${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Release>,${MY_TBB_BINARY_DIR_BASE}_release/tbb12.dll,$<IF:$<CONFIG:Debug>,${MY_TBB_BINARY_DIR_BASE}_debug/tbb12_debug.dll,${MY_TBB_BINARY_DIR_BASE}_relwithdebinfo/tbb12.dll>>
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
	configure_file("${CMAKE_CURRENT_LIST_DIR}/setup_client.iss.in"
				   "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss")
	set(no_copy $<NOT:$<CONFIG:RelWithDebInfo>>)
	add_custom_command(
		TARGET JammerNetzClient POST_BUILD
		COMMAND  "${CMAKE_COMMAND}" -E $<${no_copy}:echo> $<${no_copy}:"Not building Installer with "> chdir . ${INNOSETUP} "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss"
		DEPENDS setup_client.iss
	)
ENDIF()

IF(APPLE)
	# Moving icon into application bundle here.
	# See second answer from: https://stackoverflow.com/questions/22745728/cmake-set-icon-for-a-mac-os-x-app
	SET(ICON_FILE_NAME JammerNetzIcon)
	SET(JAMMERNETZ_ICON ${CMAKE_CURRENT_SOURCE_DIR}/Resources/${ICON_FILE_NAME}.icns)
	SET_SOURCE_FILES_PROPERTIES(${JAMMERNETZ_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

	# For apple, set the properties file to be included in the link. This is important to allow Microphone access on macOS (you will get a dialog asking for permission)
	SET_TARGET_PROPERTIES(JammerNetzClient PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_BUNDLE_NAME "${EXECUTABLE_BASE_NAME} ${JammerNetzClient_VERSION}"
		MACOSX_BUNDLE_ICON_FILE ${ICON_FILE_NAME}
		MACOSX_BUNDLE_BUNDLE_NAME ${EXECUTABLE_BASE_NAME}
		MACOSX_BUNDLE_BUNDLE_VERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
		MACOSX_BUNDLE_GUI_IDENTIFIER com.christofmuc.jammernetz
		MACOSX_BUNDLE_SIGNATURE jmrn
		CPACK_BUNDLE_STARTUP_COMMAND $<TARGET_FILE:JammerNetzClient>
		MACOSX_BUNDLE_BUNDLE_VERSION ${JammerNetzClient_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "Copyright 2021-2024 Christof Ruch"
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
		)

	IF(CPACK_BUNDLE_APPLE_CERT_APP)
	message("Certifing!!!!!!")
    INSTALL(CODE "include(BundleUtilities)
	set(BU_CHMOD_BUNDLE_ITEMS TRUE)
			message(STATUS \"Integrating dynlibs and signing app bundle \$<TARGET_BUNDLE_DIR:JammerNetzClient>\")
			fixup_bundle(\"\$<TARGET_BUNDLE_DIR:JammerNetzClient>\"  \"\"  \"\" IGNORE_ITEM \"Python\")
			execute_process(COMMAND codesign --force --deep --sign \"${CPACK_BUNDLE_APPLE_CERT_APP}\" \"\$<TARGET_BUNDLE_DIR:JammerNetzClient>\" COMMENT \"Signing app bundle\")
		")
	ELSE()
		INSTALL(CODE "include(InstallRequiredSystemLibraries)
		    include(BundleUtilities)
			set(BU_CHMOD_BUNDLE_ITEMS TRUE)
			message(STATUS \"Integrating dynlibs into app bundle \$<TARGET_BUNDLE_DIR:JammerNetzClient>\")
			fixup_bundle(\"\$<TARGET_BUNDLE_DIR:JammerNetzClient>\"  \"\"  \"${DIRS}\" IGNORE_ITEM \"Python\")
		")
	ENDIF()

	# This is supposed to build a relocatable macOS DMG installer when you specify
    # --target package
    SET(DIRS "${CMAKE_BINARY_DIR}")
    INSTALL(TARGETS JammerNetzClient
        BUNDLE DESTINATION .
        #RUNTIME # This would be a regular install like for Unix
        )

	#add_custom_command(TARGET JammerNetzClient
#		POST_BUILD COMMAND
#		${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/../Frameworks/"
#        $<TARGET_BUNDLE_DIR:JammerNetzClient>/Contents/MacOS/${EXECUTABLE_BASE_NAME}
#	)

    # Setup CPack variables
	set(CPACK_PACKAGE_NAME ${INSTALLER_BASE_NAME})
	set(CPACK_PACKAGE_ICON ${JAMMERNETZ_ICON})
    set(CPACK_PACKAGE_VERSION ${JammerNetzClient_VERSION})
    set(CPACK_PACKAGE_VENDOR "Christof Ruch Beratungs UG (haftungsbeschraenkt)")
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/Redist/agpl-3.0.txt)

	# Debugging options
	#SET(CPACK_PACKAGE_DIRECTORY /tmp/cpack-test)

    # Select only the dmg generator for now
    SET(CPACK_BINARY_DRAGNDROP ON)
    SET(CPACK_BINARY_TGZ OFF)
    SET(CPACK_BINARY_STGZ OFF)
    INCLUDE(CPack)
ENDIF()
