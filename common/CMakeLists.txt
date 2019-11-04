

include_directories("../Server/JuceLibraryCode" ${JUCE_INCLUDE_DIR})

set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR})
capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS JammerNetzPackages.capnp)

set(Sources
	BuffersConfig.h
	CMakeLists.txt
	JammerNetzPackage.cpp
	JammerNetzPackage.h	
	${CAPNP_SRCS}
	${CAPNP_HDRS}
	PacketStreamQueue.cpp
	PacketStreamQueue.h
	RandomNumbers.bin
	Recorder.cpp
	Recorder.h
	RingOfAudioBuffers.h
	ServerInfo.cpp
	ServerInfo.h
	Settings.cpp
	Settings.h
	StreamLogger.cpp
	StreamLogger.h
)

 
add_library(JammerCommon ${Sources})
target_link_libraries(JammerCommon PRIVATE CapnProto::capnp)