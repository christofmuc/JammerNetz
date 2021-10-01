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
#include "Encryption.h"
#include "Data.h"

Client::Client(DatagramSocket& socket) : socket_(socket), messageCounter_(10) /* TODO - because of the pre-fill on server side, can't be 0 */
	, currentBlockSize_(0), fecBuffer_(16), useFEC_(false), serverPort_(7777), useLocalhost_(false)
{
	// Create listeners to get notified if the application state we depend on changes
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_CRYPTOPATH, nullptr), [this](Value& value) {
		std::shared_ptr<MemoryBlock> cryptokey;
		String newCryptopath = value.getValue();
		if (newCryptopath.isNotEmpty()) {
			UDPEncryption::loadKeyfile(newCryptopath.toRawUTF8(), &cryptokey);	
			if (cryptokey) {
				setCryptoKey(cryptokey->getData(), safe_sizet_to_int(cryptokey->getSize()));
			}
			else {
				// Turn off encrpytion
				setCryptoKey(nullptr, 0);
			}
		}
		else {
			// Turn off encrpytion
			setCryptoKey(nullptr, 0);
		}
	}));
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_USE_FEC, nullptr), [this](Value& value) {
		useFEC_ = value.getValue();
	}));
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_SERVER_NAME, nullptr), [this](Value& value) {
		ScopedLock lock(serverLock_);
		serverName_ = value.getValue();
	}));
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_SERVER_PORT, nullptr), [this](Value& value) {
		String serverPortAsString = value.getValue();
		serverPort_ = strtol(serverPortAsString.toRawUTF8(), nullptr, 10); // https://forum.juce.com/t/string-to-float-int-bool/16733/12
		if (serverPort_ == 0) {
			serverPort_ = 7777; // Default value
		}
	}));
	listeners_.push_back(std::make_unique<ValueListener>(Data::instance().get().getPropertyAsValue(VALUE_USE_LOCALHOST, nullptr), [this](Value& value) {
		useLocalhost_ = value.getValue();
	}));

	// To read the current state, just execute each of these listeners once!
	std::for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });
}

Client::~Client()
{
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

bool Client::sendData(JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer)
{
	// If we have FEC data, and the user enabled it, append the last block sent
	std::shared_ptr<AudioBlock> fecBlock;
	if (useFEC_ && !fecBuffer_.isEmpty()) {
		fecBlock = fecBuffer_.getLast();
	}

	// Create a message
	JammerNetzAudioData audioMessage(messageCounter_, Time::getMillisecondCounterHiRes(), channelSetup, SAMPLE_RATE, audioBuffer, fecBlock);
	
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

	// Send off to server
	String servername;
	{
		ScopedLock lock(serverLock_);
		servername = serverName_;
	}

	if (useLocalhost_) {
		servername = "127.0.0.1";
	}

	if (blowFish_) {
		ScopedLock blowfishLock(blowFishLock_);
		int encryptedLength = blowFish_->encrypt(sendBuffer_, totalBytes, MAXFRAMESIZE);
		if (encryptedLength == -1) {
			std::cerr << "Fatal: Couldn't encrypt package, not sending to server!" << std::endl;
			return false;
		}
		sendData(servername, serverPort_, sendBuffer_, encryptedLength);
		currentBlockSize_ = encryptedLength;
	}
	else {
		// No encryption key loaded - send unencrypted Audio stream through the Internet. This is for testing only, 
		// and probably at some point should be disabled again ;-O
		if (sizet_is_safe_as_int(totalBytes)) {
			sendData(servername, serverPort_, sendBuffer_, static_cast<int>(totalBytes));
			currentBlockSize_ = static_cast<int>(totalBytes);
		}
	}

	return true;
}

int Client::getCurrentBlockSize() const
{
	return currentBlockSize_;
}

