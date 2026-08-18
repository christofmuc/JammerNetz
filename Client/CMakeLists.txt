#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzClient" VERSION 2.4.1)

configure_file("Source/version.cpp.in" "Source/version.cpp")

# Load the macro to process binary resources
include(BinaryResources)

# Setup some additional include directories for our header only libraries and those without CMake support
include_directories("${asiosdk_SOURCE_DIR}/common"
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

set(CORE_AUDIO_PROCESSING_FILES
	Source/AtomicSharedPtr.h
	Source/AudioPacketSink.h
	Source/BoundedSpscQueue.h
	Source/RealtimeAudioFrames.h
	Source/AudioTransmitWorker.cpp
	Source/AudioTransmitWorker.h
	Source/AudioReceiveWorker.cpp
	Source/AudioReceiveWorker.h
	Source/AudioRecordingWorker.cpp
	Source/AudioRecordingWorker.h
	Source/JammerNetzAudioEngine.cpp
	Source/JammerNetzAudioEngine.h
	Source/MidiSendThread.cpp Source/MidiSendThread.h
	Source/Tuner.cpp
	Source/Tuner.h
	Source/MidiPlayAlong.cpp Source/MidiPlayAlong.h
)
set(AUDIO_PROCESSING_FILES
	Source/AudioCorrectness.h
	Source/AudioCallback.cpp
	Source/AudioCallback.h
	Source/AudioService.cpp
	Source/AudioService.h
)
source_group(Source\\AudioProcessing FILES ${CORE_AUDIO_PROCESSING_FILES} ${AUDIO_PROCESSING_FILES})

set(NETWORK_FILES
	Source/JammerNetzSession.cpp
	Source/JammerNetzSession.h
	Source/Client.cpp
	Source/Client.h
	Source/DataReceiveThread.cpp
	Source/DataReceiveThread.h
	Source/PathMtuDiscovery.cpp
	Source/PathMtuDiscovery.h
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
	Source/IncludeFFMeters.h
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

add_library(JammerNetzCore STATIC ${CORE_AUDIO_PROCESSING_FILES} ${NETWORK_FILES})
target_include_directories(JammerNetzCore PUBLIC Source "${CMAKE_CURRENT_BINARY_DIR}/Source")
target_link_libraries(JammerNetzCore PUBLIC ${JUCE_LIBRARIES} JammerCommon juce-utils midikraft-base ff_meters)

add_executable(JammerNetzClient MACOSX_BUNDLE WIN32 ${SOURCES} ${DS_SOURCES} ${UICOMPONENT_FILES} ${PLAYALONG_FILES} ${AUDIO_PROCESSING_FILES} ${MIDI_FILES} ${RESOURCE_FILES} ${INSTALLER_FILES})
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

set(TESTNAME ClientCorrectnessTest)
add_executable(${TESTNAME}
	Source/AudioCorrectnessTests.cpp Source/AudioCorrectness.h
	Source/PathMtuDiscovery.cpp Source/PathMtuDiscovery.h)
target_include_directories(${TESTNAME} PRIVATE Source)
target_link_libraries(${TESTNAME} PRIVATE gtest gtest_main)
jammernetz_copy_msvc_debug_runtime(${TESTNAME})
gtest_add_tests(TARGET ${TESTNAME} SOURCES Source/AudioCorrectnessTests.cpp TEST_LIST CLIENT_CORRECTNESS_UNIT_TESTS)
set_tests_properties(${CLIENT_CORRECTNESS_UNIT_TESTS} PROPERTIES LABELS unit TIMEOUT 30)
set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)

set(CORE_TEST_NAME JammerNetzCoreTest)
add_executable(${CORE_TEST_NAME} Source/JammerNetzAudioEngineTests.cpp)
target_link_libraries(${CORE_TEST_NAME} PRIVATE JammerNetzCore JammerNetzTestSupport gtest gtest_main)
jammernetz_copy_msvc_debug_runtime(${CORE_TEST_NAME})
gtest_discover_tests(${CORE_TEST_NAME} PROPERTIES LABELS unit TIMEOUT 30)
set_target_properties(${CORE_TEST_NAME} PROPERTIES FOLDER tests)

target_include_directories(JammerNetzClient PRIVATE "${SENTRY_INCLUDE_DIR}"  "${CMAKE_CURRENT_BINARY_DIR}/Source")
IF(WIN32)
	target_link_libraries(JammerNetzClient JammerNetzCore ${DS_LIBRARIES} juce-widgets ${SENTRY_LIB})
elseif(APPLE)
	target_link_libraries(JammerNetzClient JammerNetzCore ${DS_LIBRARIES} juce-widgets)
	# ${SENTRY_LIB})
ELSE()
	target_link_libraries(JammerNetzClient
		JammerNetzCore
		${DS_LIBRARIES}
		Xext
		X11
		pthread
		${CMAKE_DL_LIBS}
		freetype
		curl
		asound
		${SENTRY_LIB}
		juce-widgets)
ENDIF()

# Pedantic about warnings
if (MSVC)
    # warning level 4 and all warnings as errors
    target_compile_options(JammerNetzClient PRIVATE /W4 /WX)
elseif(APPLE)
	target_compile_options(JammerNetzClient PRIVATE -Wall -Wextra -Werror -Wno-main -Wno-unknown-pragmas)
else()
	# Some sort of Linux
    # lots of warnings and all warnings as errors
    target_compile_options(JammerNetzClient PRIVATE -Wno-unknown-pragmas)
endif()

IF(WIN32)
	if(SENTRY_CRASH_REPORTING)
		# Post build commands - copy the optional crash-reporting runtime.
		add_custom_command(
				TARGET JammerNetzClient POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
						${SENTRY_DISTRIBUTION_FILES}
						$<TARGET_FILE_DIR:JammerNetzClient>)
	endif()
	jammernetz_copy_msvc_debug_runtime(JammerNetzClient)
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
	if(BUILD_JAMMERNETZ_PLUGIN)
		set(JAMMERNETZ_INSTALL_VST3 1)
		set(JAMMERNETZ_VST3_BUNDLE_DIR
			"$<TARGET_GENEX_EVAL:JammerNetzPlugin_VST3,$<TARGET_PROPERTY:JammerNetzPlugin_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
	else()
		set(JAMMERNETZ_INSTALL_VST3 0)
		set(JAMMERNETZ_VST3_BUNDLE_DIR "")
	endif()
	if(CMAKE_CONFIGURATION_TYPES)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo" JAMMERNETZ_CLIENT_OUTPUT_DIR)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/../Server/RelWithDebInfo" JAMMERNETZ_SERVER_OUTPUT_DIR)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/../RelWithDebInfo" JAMMERNETZ_ROOT_OUTPUT_DIR)
	else()
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" JAMMERNETZ_CLIENT_OUTPUT_DIR)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/../Server" JAMMERNETZ_SERVER_OUTPUT_DIR)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/.." JAMMERNETZ_ROOT_OUTPUT_DIR)
	endif()
	configure_file("${CMAKE_CURRENT_LIST_DIR}/setup_client.iss.in"
				   "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss.genex")
	file(GENERATE
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/setup_client-$<CONFIG>.iss"
		INPUT "${CMAKE_CURRENT_BINARY_DIR}/setup_client.iss.genex")
	set(no_copy $<NOT:$<CONFIG:RelWithDebInfo>>)
	if(INNOSETUP)
		add_custom_command(
			TARGET JammerNetzClient POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E $<${no_copy}:echo> $<${no_copy}:"Not building Installer with "> chdir . ${INNOSETUP} "${CMAKE_CURRENT_BINARY_DIR}/setup_client-$<CONFIG>.iss"
		)
	endif()
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

	# BundleUtilities must run before signing because it copies and rewrites
	# dynamic libraries inside the application bundle.
	SET(DIRS "${CMAKE_BINARY_DIR}")
	IF(CPACK_BUNDLE_APPLE_CERT_APP)
		message(STATUS "The packaged macOS application will be signed with '${CPACK_BUNDLE_APPLE_CERT_APP}'")
		INSTALL(CODE "include(BundleUtilities)
			set(BU_CHMOD_BUNDLE_ITEMS TRUE)
			message(STATUS \"Integrating dynlibs into app bundle \$<TARGET_BUNDLE_DIR:JammerNetzClient>\")
			fixup_bundle(\"\$<TARGET_BUNDLE_DIR:JammerNetzClient>\" \"\" \"${DIRS}\" IGNORE_ITEM \"Python\")
			execute_process(
				COMMAND /bin/bash \"${CMAKE_SOURCE_DIR}/scripts/macos-sign-bundle.sh\"
					\"\$<TARGET_BUNDLE_DIR:JammerNetzClient>\"
					\"${CPACK_BUNDLE_APPLE_CERT_APP}\"
					\"${JAMMERNETZ_CODESIGN_KEYCHAIN}\"
				RESULT_VARIABLE JAMMERNETZ_CODESIGN_RESULT)
			if(NOT JAMMERNETZ_CODESIGN_RESULT EQUAL 0)
				message(FATAL_ERROR \"Failed to sign JammerNetzClient.app\")
			endif()
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
