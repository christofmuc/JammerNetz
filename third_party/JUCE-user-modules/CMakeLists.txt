#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

project("ff_meters")

#file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/JuceLibraryCode/JuceHeader.h "#include <ff_meters/ff_meters.h>")

include_directories(${CMAKE_CURRENT_LIST_DIR})

set(ff_meters_sources
	ff_meters/ff_meters.h
	ff_meters/ff_meters_LevelMeter.cpp
	ff_meters/ff_meters_LevelMeter.h
	ff_meters/ff_meters_LevelMeterSource.h
	ff_meters/ff_meters_LookAndFeel.h
	ff_meters/ff_meters_LookAndFeelMethods.h
	ff_meters/ff_meters_OutlineBuffer.h
	ff_meters/ff_meters_SoundFieldLookAndFeelMethods.h
	ff_meters/ff_meters_StereoFieldBuffer.h
	ff_meters/ff_meters_StereoFieldComponent.h
	JuceLibraryCode/JuceHeader.h
)

add_library(ff_meters ${ff_meters_sources})
target_include_directories(ff_meters INTERFACE ${CMAKE_CURRENT_LIST_DIR} PRIVATE ${JUCE_INCLUDES})
target_link_libraries(ff_meters PRIVATE juce-static)
IF (WIN32)
	# Suppress warnings while compiling ff_meters because I nearly get a heart attack when that many warnings pop up on my screen
	target_compile_options(ff_meters PRIVATE /wd4244)
ENDIF()


