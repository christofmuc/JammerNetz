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

#include "ServerLogger.h"

#include "version.cpp"

#ifdef WIN32
#include <conio.h> // _kbhit()
#endif

#include <curses.h>

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
		sendThread_->signalThreadShouldExit();
		mixerThread_->signalThreadShouldExit();
		acceptThread_->signalThreadShouldExit();
		acceptThread_->stopThread(1000);
		mixerThread_->stopThread(1000);
		sendThread_->stopThread(1000);

		socket_.shutdown();
	}

	void launchServer() {
		acceptThread_->startThread();
		sendThread_->startThread();
		mixerThread_->startThread();
#ifdef WIN32
		ServerLogger::printAtPosition(0, 0, String("Starting JammerNetz server version " + getServerVersion() + ", press any key to stop").toRawUTF8());
		ServerLogger::printColumnHeader(2);
		while (!_kbhit()) {
#else
		ServerLogger::printAtPosition(0, 0, String("Starting JammerNetz server version " + getServerVersion() + ", use CTRL-C to stop").toRawUTF8());
		ServerLogger::printColumnHeader(2);
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
	std::shared_ptr<MemoryBlock> cryptoKey;

	// Parse command line arguments
	ArgumentList args(argc, argv);

	// Sweet executable name
	File myself(args.executableName);
	String shortExeName = myself.getFileName();

	// Specify commands
	ConsoleApplication app;
	app.addHelpCommand("--help|-h", "This is the JammerNetzServer " + String(getServerVersion()) + "\n\n  " + shortExeName + " --key=<key file> [--buffer=<buffer count>] [--wait=<buffer count>] [--prefill=<buffer count>]\n\n" +
		"or\n\n  " + shortExeName + " -k <key file> [-b <buffer count>] [-w <buffer count>] [-p <buffer count>]\n\n", true);
	app.addVersionCommand("--version|-v", "JammerNetzServer " + String(getServerVersion()));
	app.addDefaultCommand({ "launch", "-k <key file>", "Launch the JammerNetzServer", "Use this to launch the server in the foreground", [&](const auto &args) {
		args.failIfOptionIsMissing("--key|-k");
		if (args.containsOption("--key|-k")) { //, "crypto key", "Crypto key file name", "Specify the file name of the file containing the crypto key to use", [&](const ArgumentList &args) {
			File file(args.getFileForOption("--key|-k"));
			// Try to load Crypto file
			if (!file.existsAsFile() || !UDPEncryption::loadKeyfile(file.getFullPathName().toStdString().c_str(), &cryptoKey)) {
				app.fail("Failed to load crypto file from file " + file.getFullPathName(), -1);
			}
		}
		if (args.containsOption("--buffer|-b")) { //, "block count", "Length of buffer in blocks", "Specify the length of the incoming jitter buffer in blocks", [&](const ArgumentList &args) {
			bufferConfig.serverIncomingJitterBuffer = args.getValueForOption("--buffer|-b").getIntValue();
		}
		if (args.containsOption("--wait|-w")) { //, "block count", "Maximum length of wait buffer", "Specify the maximum length of the incoming buffer in blocks before continuing mixing", [&](const ArgumentList &args) {
			bufferConfig.serverIncomingMaximumBuffer = args.getValueForOption("--wait|-w").getIntValue();
		}
		if (args.containsOption("--prefill|-p")) { //, "block count", "Length of prefill buffer in blocks", "Specify the number of packets a client needs to send before becoming part of the mix (minimum queue length)", [&](const ArgumentList &args) {
			bufferConfig.serverBufferPrefillOnConnect = args.getValueForOption("--prefill|-p").getIntValue();
		}

		// Try to open screen
		ServerLogger::init();

		// Create Server
		Server server(cryptoKey, bufferConfig);
		server.launchServer();

		// Close screen
		ServerLogger::deinit();

		return 0;
		} });

	app.findAndRunCommand(args);

}
