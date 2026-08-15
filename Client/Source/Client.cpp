/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Client.h"

#include "JammerNetzPackage.h"
#include "ServerInfo.h"
#include "StreamLogger.h"

#include "BuffersConfig.h"
#include "XPlatformUtils.h"

Client::Client(DatagramSocket& socket) : socket_(socket), messageCounter_(10) /* TODO - because of the pre-fill on server side, can't be 0 */
	, currentBlockSize_(0), useFEC_(false), serverPort_(7777), useLocalhost_(false), fecBuffer_(16)
{
}

Client::~Client()
{
}

void Client::setServer(const juce::String& serverName, int serverPort, bool useLocalhost)
{
	const juce::ScopedLock lock(serverLock_);
	serverName_ = serverName;
	serverPort_.store(serverPort > 0 ? serverPort : 7777, std::memory_order_relaxed);
	useLocalhost_.store(useLocalhost, std::memory_order_relaxed);
}

void Client::setUseFEC(bool enabled)
{
	useFEC_.store(enabled, std::memory_order_relaxed);
	nlohmann::json fecControl;
	fecControl["FEC"] = enabled;
	sendControl(fecControl);
}

void Client::setCryptoKey(const void* keyData, int keyBytes)
{
	ScopedLock blowfishLock(blowFishLock_);
	if (keyData)
	{
		blowFish_ = std::make_unique<BlowFish>(keyData, keyBytes);
	}
	else {
		blowFish_.reset();
	}
}

bool Client::sendData(String const &remoteHostname, int remotePort, void *data, int numbytes) {
	// Writing will block until the socket is ready to write
	auto bytesWritten = socket_.write(remoteHostname, remotePort, data, numbytes);
	if (bytesWritten == -1 || bytesWritten != numbytes) {
		// This is bad - when could this happen?
		// Well, for once, if the remoteHostname is empty or incorrect
		return false;
	}
	return true;
}

bool Client::sendData(JammerNetzChannelSetup const& channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer, ControlData controllers) {
    ScopedLock lockSocket(socketLock_);

    // If we have FEC data, and the user enabled it, append the last block sent
    std::shared_ptr<AudioBlock> fecBlock;
    //if (useFEC_ && !fecBuffer_.isEmpty()) {
    //    fecBlock = fecBuffer_.getLast();
    //}
    MidiSignal toSend = MidiSignal_None;
    if (controllers.midiSignal.has_value()) {
        toSend = *controllers.midiSignal;
    }

    // Create a message
    JammerNetzAudioData audioMessage(messageCounter_, Time::getMillisecondCounterHiRes(), channelSetup, SAMPLE_RATE,
                                     controllers.bpm, toSend, audioBuffer, fecBlock);

    messageCounter_++;
    size_t totalBytes;
    audioMessage.serialize(sendBuffer_, totalBytes);

    // Store the audio data somewhere else because we need it for forward error correction
    std::shared_ptr<AudioBlock> redundencyData = std::make_shared<AudioBlock>();
    redundencyData->messageCounter = audioMessage.messageCounter();
    redundencyData->timestamp = audioMessage.timestamp();
    redundencyData->channelSetup = channelSetup;
    redundencyData->audioBuffer = std::make_shared<AudioBuffer<float>>();
    *redundencyData->audioBuffer = *audioBuffer; // Deep copy
    fecBuffer_.push(redundencyData);
    return sendBufferToServer(totalBytes);
}

bool Client::sendBufferToServer(size_t totalBytes)
{
	// Send off to server
	String servername;
	int serverPort;
	bool useLocalhost;
	{
		ScopedLock lock(serverLock_);
		servername = serverName_;
		serverPort = serverPort_.load(std::memory_order_relaxed);
		useLocalhost = useLocalhost_.load(std::memory_order_relaxed);
	}

	if (useLocalhost) {
		servername = "127.0.0.1";
	}

	{
		ScopedLock blowfishLock(blowFishLock_);
		if (blowFish_) {
			int encryptedLength = blowFish_->encrypt(sendBuffer_, totalBytes, MAXFRAMESIZE);
			if (encryptedLength == -1) {
				std::cerr << "Fatal: Couldn't encrypt package, not sending to server!" << std::endl;
				return false;
			}
			const bool sent = sendData(servername, serverPort, sendBuffer_, encryptedLength);
			if (sent) {
				currentBlockSize_ = encryptedLength;
			}
			return sent;
		}
	}

	// No encryption key loaded - send unencrypted Audio stream through the Internet. This is for testing only,
	// and probably at some point should be disabled again ;-O
	if (!sizet_is_safe_as_int(totalBytes)) {
		return false;
	}

	const int bytesToSend = static_cast<int>(totalBytes);
	const bool sent = sendData(servername, serverPort, sendBuffer_, bytesToSend);
	if (sent) {
		currentBlockSize_ = bytesToSend;
	}
	return sent;
}

bool Client::sendControl(nlohmann::json &json)
{
    ScopedLock lockSocket(socketLock_);

    JammerNetzControlMessage controlMessage(json);
    size_t totalBytes;
    controlMessage.serialize(sendBuffer_, totalBytes);
    if (totalBytes > 0) {
        return sendBufferToServer(totalBytes);
    }
    else
    {
        return false;
    }
}

int Client::getCurrentBlockSize() const
{
	return currentBlockSize_;
}
