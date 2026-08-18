#
#  Copyright (c) 2026 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("JammerNetzPlugin" VERSION 2.4.1)

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
else()
	target_compile_options(JammerNetzPlugin PRIVATE -Wall -Wextra -Werror -Wno-unknown-pragmas)
endif()

set(PLUGIN_TEST_NAME JammerNetzPluginTest)
add_executable(${PLUGIN_TEST_NAME} Source/JammerNetzPluginTests.cpp)
target_link_libraries(${PLUGIN_TEST_NAME} PRIVATE JammerNetzPlugin gtest gtest_main)

gtest_discover_tests(${PLUGIN_TEST_NAME} DISCOVERY_MODE PRE_TEST)
set_target_properties(${PLUGIN_TEST_NAME} PROPERTIES FOLDER tests)
