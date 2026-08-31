set(COMPATIBILITY_SNAPSHOT_ROOT "${CMAKE_CURRENT_LIST_DIR}/snapshots/2.4.2")
set(COMPATIBILITY_LEGACY_COMMON "${COMPATIBILITY_SNAPSHOT_ROOT}/common")
set(COMPATIBILITY_LEGACY_CLIENT "${COMPATIBILITY_SNAPSHOT_ROOT}/Client/Source")
set(COMPATIBILITY_LEGACY_SERVER "${COMPATIBILITY_SNAPSHOT_ROOT}/Server/Source")

add_library(JammerNetz242Common STATIC
	${COMPATIBILITY_LEGACY_COMMON}/Encryption.cpp
	${COMPATIBILITY_LEGACY_COMMON}/JammerNetzClientInfoMessage.cpp
	${COMPATIBILITY_LEGACY_COMMON}/JammerNetzPackage.cpp
	${COMPATIBILITY_LEGACY_COMMON}/PacketStreamQueue.cpp
	${COMPATIBILITY_LEGACY_COMMON}/Recorder.cpp
	${COMPATIBILITY_LEGACY_COMMON}/RunningStats.cpp
	${COMPATIBILITY_LEGACY_COMMON}/ServerInfo.cpp
)
target_include_directories(JammerNetz242Common
	PUBLIC
		"${COMPATIBILITY_LEGACY_COMMON}"
		"${CMAKE_BINARY_DIR}/common"
		"${CMAKE_SOURCE_DIR}/third_party/flatbuffers/include"
)
target_link_libraries(JammerNetz242Common PRIVATE juce-utils)
# The 2.4.2 FlatBuffers schemas are unchanged, so the frozen serializer can use
# the generated headers from the candidate build. Keep this as a build-order
# dependency only; the released peer never links the candidate JammerCommon.
add_dependencies(JammerNetz242Common JammerCommon)

add_library(JammerNetz242Core STATIC
	${COMPATIBILITY_LEGACY_CLIENT}/AudioRecordingWorker.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/AudioReceiveWorker.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/AudioTransmitWorker.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/Client.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/DataReceiveThread.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/JammerNetzAudioEngine.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/JammerNetzSession.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/MidiPlayAlong.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/MidiSendThread.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/PathMtuDiscovery.cpp
	${COMPATIBILITY_LEGACY_CLIENT}/Tuner.cpp
)
target_include_directories(JammerNetz242Core
	PUBLIC
		"${COMPATIBILITY_LEGACY_CLIENT}"
		"${COMPATIBILITY_LEGACY_COMMON}"
	PRIVATE
		"${asiosdk_SOURCE_DIR}/common"
		"${CMAKE_SOURCE_DIR}/third_party/infra/include"
		"${CMAKE_SOURCE_DIR}/third_party/Q/q_lib/include"
		"${CMAKE_SOURCE_DIR}/third_party"
)
target_link_libraries(JammerNetz242Core
	PUBLIC
		${JUCE_LIBRARIES}
		JammerNetz242Common
		juce-utils
		midikraft-base
		ff_meters
)

add_library(JammerNetz242ServerCore STATIC
	${COMPATIBILITY_LEGACY_SERVER}/ClientState.cpp
	${COMPATIBILITY_LEGACY_SERVER}/ServerMixScheduler.cpp
	${COMPATIBILITY_LEGACY_SERVER}/ServerMixerCore.cpp
)
target_include_directories(JammerNetz242ServerCore
	PUBLIC
		"${COMPATIBILITY_LEGACY_SERVER}"
		"${COMPATIBILITY_LEGACY_COMMON}"
)
target_link_libraries(JammerNetz242ServerCore
	PUBLIC
		JammerNetz242Common
		juce-utils
		TBB::tbb
		${JUCE_LIBRARIES}
)

add_executable(JammerNetz242CompatibilityPeer CompatibilityPeer.cpp)
target_compile_definitions(JammerNetz242CompatibilityPeer PRIVATE
	JAMMERNETZ_COMPAT_VERSION="2.4.2")
target_include_directories(JammerNetz242CompatibilityPeer PRIVATE
	"${COMPATIBILITY_LEGACY_CLIENT}"
	"${COMPATIBILITY_LEGACY_SERVER}"
	"${COMPATIBILITY_LEGACY_COMMON}"
)
target_link_libraries(JammerNetz242CompatibilityPeer PRIVATE
	JammerNetz242Core
	JammerNetz242ServerCore
	nlohmann_json::nlohmann_json
)

add_executable(JammerNetzCurrentCompatibilityPeer CompatibilityPeer.cpp)
target_compile_definitions(JammerNetzCurrentCompatibilityPeer PRIVATE
	JAMMERNETZ_COMPAT_VERSION="candidate"
	JAMMERNETZ_COMPAT_CANDIDATE=1)
target_link_libraries(JammerNetzCurrentCompatibilityPeer PRIVATE
	JammerNetzCore
	JammerNetzServerCore
	nlohmann_json::nlohmann_json
)

foreach(target JammerNetz242CompatibilityPeer JammerNetzCurrentCompatibilityPeer)
	jammernetz_copy_msvc_debug_runtime(${target})
	jammernetz_copy_tbb_runtime(${target})
	set_target_properties(${target} PROPERTIES FOLDER tests/compatibility)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic -Werror)
	endif()
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ReleaseCompatibilityTest
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_release_compatibility.py"
		--legacy "$<TARGET_FILE:JammerNetz242CompatibilityPeer>"
		--candidate "$<TARGET_FILE:JammerNetzCurrentCompatibilityPeer>"
		--artifact-dir "${CMAKE_BINARY_DIR}/test-artifacts/compatibility/candidate-against-2.4.2")
set_tests_properties(ReleaseCompatibilityTest PROPERTIES LABELS compatibility TIMEOUT 180)

add_test(NAME ReleaseCompatibilitySnapshotTest
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/manage_snapshot.py"
		--verify --destination "${COMPATIBILITY_SNAPSHOT_ROOT}")
set_tests_properties(ReleaseCompatibilitySnapshotTest PROPERTIES LABELS compatibility TIMEOUT 30)
