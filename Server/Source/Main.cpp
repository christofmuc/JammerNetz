/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

#include "MixerThread.h"
#include "AcceptThread.h"
#include "SendThread.h"
#include "Encryption.h"

#include "BuffersConfig.h"
#include "Recorder.h"

#include "version.cpp"

#ifdef WIN32
#include <conio.h> // _kbhit()
#endif

class Server {
public:
	Server(std::shared_ptr<MemoryBlock> cryptoKey, ServerBufferConfig bufferConfig) : mixdownRecorder_(File::getCurrentWorkingDirectory(), "mixdown", RecordingType::FLAC), clientRecorder_(File(), "input", RecordingType::AIFF),
		mixdownSetup_({ JammerNetzChannelTarget::Left, JammerNetzChannelTarget::Right }) // Setup standard mix down setup - two channels only in stereo
	{
		// Start the recorder of the mix down
		//mixdownRecorder_.updateChannelInfo(48000, mixdownSetup_);
		acceptThread_ = std::make_unique<AcceptThread>(socket_, incomingStreams_, wakeUpQueue_, bufferConfig, cryptoKey->getData(), (int) cryptoKey->getSize());
		sendThread_ = std::make_unique <SendThread>(socket_, sendQueue_, incomingStreams_, cryptoKey->getData(), (int) cryptoKey->getSize());
		mixerThread_ = std::make_unique<MixerThread>(incomingStreams_, mixdownSetup_, sendQueue_, wakeUpQueue_, mixdownRecorder_, bufferConfig);

		sendQueue_.set_capacity(128); // This is an arbitrary number only to prevent memory overflow should the sender thread somehow die (i.e. no network or something)
	}

	~Server() {
		acceptThread_->stopThread(1000);
		sendThread_->stopThread(1000);
		mixerThread_->stopThread(1000);

		socket_.shutdown();
	}

	void launchServer() {
		acceptThread_->startThread();
		sendThread_->startThread();
		mixerThread_->startThread();
#ifdef WIN32
		std::cout << "Starting JammerNetz server version " << getServerVersion() << ", press any key to stop" << std::endl;
		while (!_kbhit()) {
#else
		std::cout << "Starting JammerNetz server, using CTRL-C to stop" << std::endl;
		while (true) {
#endif
			Thread::sleep(1000);
		}
	}

private:
	DatagramSocket socket_;
	std::unique_ptr<AcceptThread> acceptThread_;
	std::unique_ptr<SendThread> sendThread_;
	std::unique_ptr<MixerThread> mixerThread_;

	TPacketStreamBundle incomingStreams_;
	TOutgoingQueue sendQueue_;
	TMessageQueue wakeUpQueue_;

	Recorder clientRecorder_; // Later I need one per client
	Recorder mixdownRecorder_;
	JammerNetzChannelSetup mixdownSetup_; // This is the same for everybody
};

int main(int argc, char *argv[])
{
	ServerBufferConfig bufferConfig;
	bufferConfig.serverIncomingJitterBuffer = SERVER_INCOMING_JITTER_BUFFER;
	bufferConfig.serverIncomingMaximumBuffer = SERVER_INCOMING_MAXIMUM_BUFFER;
	bufferConfig.serverBufferPrefillOnConnect = BUFFER_PREFILL_ON_CONNECT;

	// Check if filename for Crypto Key is given
	if (argc < 2) {
		std::cout << "Please specify the name of the crypto key file as command line argument. This should be a file containing 72 random bytes for the symmetric Blowfish encryption." << std::endl;
		return -1;
	}

	// Try to load Crypto file
	std::shared_ptr<MemoryBlock> cryptoKey;
	if (!UDPEncryption::loadKeyfile(argv[1], &cryptoKey)) {
		std::cout << "Failed to load crypto key from file " << argv[1] << ". Check the file exists." << std::endl;
		return -1;
	}

	// An additional parameter could specify the number of buffers to use
	//TODO use getopts or something
	if (argc > 2) {
		bufferConfig.serverIncomingJitterBuffer = atoi(argv[2]);
		if (bufferConfig.serverIncomingJitterBuffer == 0) {
			std::cout << "Second parameter is the number of server jitter buffers to use, please specify a number";
			return -1;
		}
	}
	if (argc > 3) {
		bufferConfig.serverIncomingMaximumBuffer= atoi(argv[3]);
		if (bufferConfig.serverIncomingMaximumBuffer == 0) {
			std::cout << "Third parameter is the maximum number of jitter buffers to fill before the one with an empty queue is kicked out";
			return -1;
		}
	}
	if (argc > 4) {
		bufferConfig.serverBufferPrefillOnConnect = atoi(argv[4]);
		if (bufferConfig.serverBufferPrefillOnConnect == 0) {
			std::cout << "Fourth parameter is the number of packets required before being allowed into the mix";
			return -1;
		}
	}

	// Create Server
	Server server(cryptoKey, bufferConfig);
	server.launchServer();
	return 0;
}
