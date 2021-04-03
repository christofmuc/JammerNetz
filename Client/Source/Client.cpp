/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Client.h"

#include "JammerNetzPackage.h"
#include "ServerInfo.h"
#include "StreamLogger.h"

#include "BuffersConfig.h"

Client::Client(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler) : messageCounter_(10) /* TODO - because of the pre-fill on server side, can't be 0 */
	, currentBlockSize_(0), fecBuffer_(16)
{
	// We will send data to the server via this port
	int randomPort = 8888 + (Random().nextInt() % 64);
	if (!socket_.bindToPort(randomPort, "0.0.0.0")) {
		jassert(false);
		StreamLogger::instance() << "FATAL: Couldn't bind to port " << randomPort << std::endl;
	}

	// Fire up the network listener thread which will receive the answers from the server
	receiver_ = std::make_unique<DataReceiveThread>(socket_, newDataHandler);
	receiver_->startThread();
}

Client::~Client()
{
	// Give the network thread a second to exit
	receiver_->stopThread(1000);
	socket_.shutdown();
}

void Client::setCryptoKey(const void* keyData, int keyBytes)
{
	{
		ScopedLock blowfishLock(blowFishLock_);
		blowFish_ = std::make_unique<BlowFish>(keyData, keyBytes);
	}
	receiver_->setCryptoKey(keyData, keyBytes);
}

bool Client::isReceivingData() const
{
	return receiver_->isReceivingData();
}

bool Client::sendData(String const &remoteHostname, int remotePort, void *data, int numbytes) {
	// Writing will block until the socket is ready to write
	auto bytesWritten = socket_.write(remoteHostname, remotePort, data, numbytes);
	if (bytesWritten == -1 || bytesWritten != numbytes) {
		//int errorcode = ::WSAGetLastError();
		//StreamLogger::instance() << "Error writing to socket! Error code is " << errorcode << std::endl;
		StreamLogger::instance() << "Error writing to socket!" << std::endl;
	}
	return true;
}

bool Client::sendData(JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer)
{
	// If we have FEC data, append the last block sent
	std::shared_ptr<AudioBlock> fecBlock;
	if (!fecBuffer_.isEmpty()) {
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
	if (blowFish_) {
		ScopedLock blowfishLock(blowFishLock_);
		int encryptedLength = blowFish_->encrypt(sendBuffer_, totalBytes, MAXFRAMESIZE);
		if (encryptedLength == -1) {
			std::cerr << "Fatal: Couldn't encrypt package, not sending to server!" << std::endl;
			return false;
		}
		sendData(ServerInfo::serverName, ServerInfo::serverPort, sendBuffer_, encryptedLength);
		currentBlockSize_ = encryptedLength;
	}
	else {
		std::cerr << "Fatal: No crypto key loaded, can't send to server!" << std::endl;
	}

	return true;
}

int Client::getCurrentBlockSize() const
{
	return currentBlockSize_;
}

double Client::currentRTT() const
{
	return receiver_->currentRTT();
}

std::shared_ptr<JammerNetzClientInfoMessage> Client::getClientInfo() const
{
	return receiver_->getClientInfo();
}

JammerNetzChannelSetup Client::getCurrentSessionSetup() const
{
	return receiver_->sessionSetup();
}

