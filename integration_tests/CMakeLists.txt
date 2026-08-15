set(SYSTEM_TEST_TARGET CleanNetworkSystemTest)

add_executable(${SYSTEM_TEST_TARGET} CleanNetworkSystemTests.cpp)
target_link_libraries(${SYSTEM_TEST_TARGET}
	PRIVATE
		JammerNetzCore
		JammerNetzServerCore
		JammerNetzTestSupport
		gtest
		gtest_main
)
jammernetz_copy_msvc_debug_runtime(${SYSTEM_TEST_TARGET})
jammernetz_copy_tbb_runtime(${SYSTEM_TEST_TARGET})
gtest_discover_tests(${SYSTEM_TEST_TARGET} PROPERTIES LABELS system TIMEOUT 30)
set_target_properties(${SYSTEM_TEST_TARGET} PROPERTIES FOLDER tests)

if(MSVC)
	target_compile_options(${SYSTEM_TEST_TARGET} PRIVATE /W4 /WX)
else()
	target_compile_options(${SYSTEM_TEST_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
