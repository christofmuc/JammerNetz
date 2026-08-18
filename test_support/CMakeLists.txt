set(TEST_SUPPORT_TARGET JammerNetzTestSupport)

add_library(${TEST_SUPPORT_TARGET} STATIC
	DeterministicAudioTestSupport.cpp
	DeterministicAudioTestSupport.h
)
target_include_directories(${TEST_SUPPORT_TARGET} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(${TEST_SUPPORT_TARGET}
	PUBLIC
		juce-static
		nlohmann_json::nlohmann_json
)

if(MSVC)
	target_compile_options(${TEST_SUPPORT_TARGET} PRIVATE /W4 /WX)
else()
	target_compile_options(${TEST_SUPPORT_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

set(TEST_SUPPORT_TEST_TARGET TestSupportTest)
add_executable(${TEST_SUPPORT_TEST_TARGET} DeterministicAudioTestSupportTests.cpp)
target_link_libraries(${TEST_SUPPORT_TEST_TARGET}
	PRIVATE
		${TEST_SUPPORT_TARGET}
		gtest
		gtest_main
)
target_compile_definitions(${TEST_SUPPORT_TEST_TARGET}
	PRIVATE JAMMERNETZ_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts"
)
jammernetz_copy_msvc_debug_runtime(${TEST_SUPPORT_TEST_TARGET})
gtest_add_tests(TARGET ${TEST_SUPPORT_TEST_TARGET}
	SOURCES DeterministicAudioTestSupportTests.cpp
	TEST_LIST TEST_SUPPORT_UNIT_TESTS
)
set_tests_properties(${TEST_SUPPORT_UNIT_TESTS} PROPERTIES LABELS unit TIMEOUT 30)
set_target_properties(${TEST_SUPPORT_TARGET} ${TEST_SUPPORT_TEST_TARGET} PROPERTIES FOLDER tests)
