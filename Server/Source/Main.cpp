/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JuceHeader.h"

#include "JammerNetzPackage.h"

#include "MixerThread.h"
#include "AcceptThread.h"
#include "SendThread.h"

#include "Recorder.h"

#ifdef WIN32
#include <conio.h> // _kbhit()
#endif

class Server {
public:
	Server() : mixdownRecorder_(File::getCurrentWorkingDirectory(), "mixdown", RecordingType::FLAC), clientRecorder_(File(), "input", RecordingType::AIFF),
		mixdownSetup_({ JammerNetzChannelTarget::Left, JammerNetzChannelTarget::Right }) // Setup standard mix down setup - two channels only in stereo
	{
		// Start the recorder of the mix down
		mixdownRecorder_.updateChannelInfo(48000, mixdownSetup_);
		acceptThread_ = std::make_unique<AcceptThread>(socket_, incomingStreams_, wakeUpQueue_);
		sendThread_ = std::make_unique <SendThread>(socket_, sendQueue_, incomingStreams_);
		mixerThread_ = std::make_unique<MixerThread>(incomingStreams_, mixdownSetup_, sendQueue_, wakeUpQueue_, mixdownRecorder_);

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
		std::cout << "Starting JammerNetz server, press any key to stop" << std::endl;
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

int main(int, char*[])
{
	// Create Server
	Server server;
	server.launchServer();
	return 0;
}
