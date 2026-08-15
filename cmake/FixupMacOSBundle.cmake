if(NOT DEFINED JAMMERNETZ_FIXUP_EXECUTABLE OR JAMMERNETZ_FIXUP_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "JAMMERNETZ_FIXUP_EXECUTABLE is required")
endif()

if(NOT DEFINED JAMMERNETZ_FIXUP_SEARCH_DIRECTORIES)
    set(JAMMERNETZ_FIXUP_SEARCH_DIRECTORIES "")
endif()

include(BundleUtilities)
set(BU_CHMOD_BUNDLE_ITEMS TRUE)

# BundleUtilities only treats directories ending in .app as bundles. Passing
# the executable is its supported path for AU/VST3 bundles; dependencies are
# copied beside the executable and their load commands are rewritten.
message(STATUS "Making macOS plug-in executable self-contained: ${JAMMERNETZ_FIXUP_EXECUTABLE}")
fixup_bundle(
    "${JAMMERNETZ_FIXUP_EXECUTABLE}"
    ""
    "${JAMMERNETZ_FIXUP_SEARCH_DIRECTORIES}")
