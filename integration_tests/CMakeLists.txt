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
gtest_discover_tests(${SYSTEM_TEST_TARGET} PROPERTIES LABELS system TIMEOUT 60)
set_target_properties(${SYSTEM_TEST_TARGET} PROPERTIES FOLDER tests)

if(MSVC)
	target_compile_options(${SYSTEM_TEST_TARGET} PRIVATE /W4 /WX)
else()
	target_compile_options(${SYSTEM_TEST_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

if(BUILD_JAMMERNETZ_SERVER)
	if(WIN32)
		find_package(pdcurses CONFIG REQUIRED)
	endif()
	set(SECURE_TRANSPORT_TEST_TARGET SecureUdpTransportSystemTest)
	add_executable(${SECURE_TRANSPORT_TEST_TARGET}
		SecureUdpTransportSystemTests.cpp
		../Server/Source/AcceptThread.cpp
		../Server/Source/SendThread.cpp
		../Server/Source/ServerLogger.cpp
	)
	target_include_directories(${SECURE_TRANSPORT_TEST_TARGET} PRIVATE ../Server/Source)
	target_link_libraries(${SECURE_TRANSPORT_TEST_TARGET}
		PRIVATE
			JammerNetzCore
			JammerNetzServerCore
			gtest
			gtest_main
	)
	if(WIN32)
		target_link_libraries(${SECURE_TRANSPORT_TEST_TARGET} PRIVATE pdcurses::pdcurses)
	else()
		target_link_libraries(${SECURE_TRANSPORT_TEST_TARGET} PRIVATE ncurses)
	endif()
	jammernetz_copy_msvc_debug_runtime(${SECURE_TRANSPORT_TEST_TARGET})
	jammernetz_copy_tbb_runtime(${SECURE_TRANSPORT_TEST_TARGET})
	gtest_discover_tests(${SECURE_TRANSPORT_TEST_TARGET} PROPERTIES LABELS system TIMEOUT 30)
	set_target_properties(${SECURE_TRANSPORT_TEST_TARGET} PROPERTIES FOLDER tests)

	if(MSVC)
		target_compile_options(${SECURE_TRANSPORT_TEST_TARGET} PRIVATE /W4 /WX)
	else()
		target_compile_options(${SECURE_TRANSPORT_TEST_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
	endif()
endif()

set(CHARACTERIZATION_TEST_TARGET NetworkImpairmentCharacterizationTest)
add_executable(${CHARACTERIZATION_TEST_TARGET}
	CharacterizationTestSupport.h
	NetworkImpairmentCharacterizationTests.cpp
	ReconnectCharacterizationTests.cpp
)
target_link_libraries(${CHARACTERIZATION_TEST_TARGET}
	PRIVATE
		JammerNetzCore
		JammerNetzServerCore
		JammerNetzTestSupport
		gtest
		gtest_main
)
target_compile_definitions(${CHARACTERIZATION_TEST_TARGET}
	PRIVATE JAMMERNETZ_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts")
jammernetz_copy_msvc_debug_runtime(${CHARACTERIZATION_TEST_TARGET})
jammernetz_copy_tbb_runtime(${CHARACTERIZATION_TEST_TARGET})
gtest_discover_tests(${CHARACTERIZATION_TEST_TARGET} PROPERTIES LABELS characterization TIMEOUT 30)
set_target_properties(${CHARACTERIZATION_TEST_TARGET} PROPERTIES FOLDER tests)

set(STRESS_TEST_TARGET ReconnectStressTest)
add_executable(${STRESS_TEST_TARGET} ReconnectStressTests.cpp)
target_link_libraries(${STRESS_TEST_TARGET}
	PRIVATE
		JammerNetzServerCore
		gtest
		gtest_main
)
jammernetz_copy_msvc_debug_runtime(${STRESS_TEST_TARGET})
jammernetz_copy_tbb_runtime(${STRESS_TEST_TARGET})
gtest_discover_tests(${STRESS_TEST_TARGET} PROPERTIES LABELS stress TIMEOUT 30)
set_target_properties(${STRESS_TEST_TARGET} PROPERTIES FOLDER tests)

if(MSVC)
	target_compile_options(${CHARACTERIZATION_TEST_TARGET} PRIVATE /W4 /WX)
	target_compile_options(${STRESS_TEST_TARGET} PRIVATE /W4 /WX)
else()
	target_compile_options(${CHARACTERIZATION_TEST_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
	target_compile_options(${STRESS_TEST_TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
