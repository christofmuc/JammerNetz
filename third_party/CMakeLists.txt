#
#  Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#  Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

add_subdirectory(JUCE-user-modules)

# Setup the one file miniaudio library for CMake
set(MINIAUDIO_SOURCES miniaudio_impl.c)

add_library(miniaudio ${MINIAUDIO_SOURCES})
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_LIST_DIR}/miniaudio)
