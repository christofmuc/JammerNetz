#
# Library for digital stage JUCE look and feel
#

include(BinaryResources)

# Create our binary resources
create_resources(Resources Resources.h)

set(SOURCES
	DSLookAndFeel.cpp DSLookAndFeel.h
	Resources.h
	"Resources/AppView BG_Gradient.svg"
	"Resources/stage_web_white.png"
)

add_library(ds_look_and_feel ${SOURCES})
target_include_directories(ds_look_and_feel INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ds_look_and_feel PRIVATE juce-utils ff_meters)

IF (WIN32)
	# Suppress warnings while compiling ff_meters because I nearly get a heart attack when that many warnings pop up on my screen
	target_compile_options(ds_look_and_feel PUBLIC /wd4244 /wd4100 /wd4244 /wd4456 /wd4702)
ENDIF()
