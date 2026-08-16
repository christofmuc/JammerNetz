#
#  Copyright (c) 2026 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzPlugin" VERSION 2.3.0)

set(JAMMERNETZ_PLUGIN_FORMATS VST3)
if(APPLE)
	list(APPEND JAMMERNETZ_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(JammerNetzPlugin
	COMPANY_NAME "Christof Ruch Beratungs UG"
	BUNDLE_ID "com.christofmuc.jammernetz.plugin"
	PLUGIN_MANUFACTURER_CODE JmNz
	PLUGIN_CODE JnP4
	FORMATS ${JAMMERNETZ_PLUGIN_FORMATS}
	PRODUCT_NAME "JammerNetz"
	DESCRIPTION "Low-latency JammerNetz send and remote return"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS FALSE
	COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(JammerNetzPlugin PRIVATE
	README.md
	Source/JammerNetzPluginEditor.cpp
	Source/JammerNetzPluginEditor.h
	Source/JammerNetzPluginProcessor.cpp
	Source/JammerNetzPluginProcessor.h)

target_include_directories(JammerNetzPlugin PUBLIC Source)
target_link_libraries(JammerNetzPlugin
	PUBLIC JammerNetzCore
	PRIVATE juce::juce_audio_utils)

target_compile_definitions(JammerNetzPlugin PUBLIC
	JUCE_VST3_CAN_REPLACE_VST2=0)

if(MSVC)
	target_compile_options(JammerNetzPlugin PRIVATE /W4 /WX)
elseif(APPLE)
	target_compile_options(JammerNetzPlugin PRIVATE -Wall -Wextra -Werror -Wno-unknown-pragmas)
	foreach(JAMMERNETZ_PLUGIN_WRAPPER JammerNetzPlugin_VST3 JammerNetzPlugin_AU)
		add_custom_command(TARGET ${JAMMERNETZ_PLUGIN_WRAPPER} POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				"-DJAMMERNETZ_FIXUP_EXECUTABLE=$<TARGET_FILE:${JAMMERNETZ_PLUGIN_WRAPPER}>"
				"-DJAMMERNETZ_FIXUP_BUNDLE=$<TARGET_BUNDLE_DIR:${JAMMERNETZ_PLUGIN_WRAPPER}>"
				"-DJAMMERNETZ_FIXUP_TBB=$<TARGET_FILE:tbb>"
				-P "${CMAKE_SOURCE_DIR}/cmake/FixupMacOSBundle.cmake"
			COMMENT "Bundling macOS runtime dependencies for ${JAMMERNETZ_PLUGIN_WRAPPER}"
			VERBATIM)
	endforeach()
else()
	target_compile_options(JammerNetzPlugin PRIVATE -Wall -Wextra -Werror -Wno-unknown-pragmas)
endif()

set(PLUGIN_TEST_NAME JammerNetzPluginTest)
add_executable(${PLUGIN_TEST_NAME} Source/JammerNetzPluginTests.cpp)
target_link_libraries(${PLUGIN_TEST_NAME} PRIVATE JammerNetzPlugin gtest gtest_main)

if(WIN32)
	set(JAMMERNETZ_TBB_RUNTIME
		${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Release>,${MY_TBB_BINARY_DIR_BASE}_release/tbb12.dll,$<IF:$<CONFIG:Debug>,${MY_TBB_BINARY_DIR_BASE}_debug/tbb12_debug.dll,${MY_TBB_BINARY_DIR_BASE}_relwithdebinfo/tbb12.dll>>)
	add_custom_command(TARGET ${PLUGIN_TEST_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${JAMMERNETZ_TBB_RUNTIME}
			$<TARGET_FILE_DIR:${PLUGIN_TEST_NAME}>)
	add_custom_command(TARGET JammerNetzPlugin_VST3 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${JAMMERNETZ_TBB_RUNTIME}
			$<TARGET_FILE_DIR:JammerNetzPlugin_VST3>)
endif()

gtest_discover_tests(${PLUGIN_TEST_NAME} DISCOVERY_MODE PRE_TEST)
set_target_properties(${PLUGIN_TEST_NAME} PROPERTIES FOLDER tests)
