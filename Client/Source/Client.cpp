/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Client.h"

//TODO this needs to go away, just used for better error messages
#include <winsock.h>

#include "JammerNetzPackage.h"
#include "ServerInfo.h"
#include "StreamLogger.h"

Client::Client(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler) : messageCounter_(10) /* TODO - because of the pre-fill on server side, can't be 0 */
	, currentBlockSize_(0), fecBuffer_(16), blowFish_(BinaryData::RandomNumbers_bin, BinaryData::RandomNumbers_binSize)
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

bool Client::isReceivingData() const
{
	return receiver_->isReceivingData();
}

bool Client::sendData(String const &remoteHostname, int remotePort, void *data, int numbytes) {
	// Writing will block until the socket is ready to write
	auto bytesWritten = socket_.write(remoteHostname, remotePort, data, numbytes);
	if (bytesWritten == -1 || bytesWritten != numbytes) {
		int errorcode = ::WSAGetLastError();
		StreamLogger::instance() << "Error writing to socket! Error code is " << errorcode << std::endl;
	}
	return true;
}

bool Client::sendData(JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer)
{
	// Create a message
	JammerNetzAudioData audioMessage(messageCounter_, Time::getMillisecondCounterHiRes(), channelSetup, audioBuffer);
	messageCounter_++;
	size_t totalBytes;
	audioMessage.serialize(sendBuffer_, totalBytes);
	// If we have FEC data, append the last block sent
	if (!fecBuffer_.isEmpty()) {
		audioMessage.serialize(sendBuffer_, totalBytes, fecBuffer_.getLast(), 48000, 2);
	}	

	// Store the audio data somewhere else because we need it for forward error correction
	std::shared_ptr<AudioBlock> redundencyData = std::make_shared<AudioBlock>();
	redundencyData->messageCounter = audioMessage.messageCounter();
	redundencyData->timestamp = audioMessage.timestamp();
	redundencyData->channelSetup = channelSetup;
	redundencyData->audioBuffer = std::make_shared<AudioBuffer<float>>();
	*redundencyData->audioBuffer = *audioBuffer; // Deep copy
	fecBuffer_.push(redundencyData);

	// Send off to server
	int encryptedLength = blowFish_.encrypt(sendBuffer_, totalBytes, MAXFRAMESIZE);
	if (encryptedLength == -1) {
		StreamLogger::instance() << "Fatal: Couldn't encrypt package, not sending to server!" << std::endl;
		return false;
	}
	sendData(ServerInfo::serverName, ServerInfo::serverPort, sendBuffer_, encryptedLength);
	currentBlockSize_ = encryptedLength;

	if (numberOfFlares_ > 0) {
		// Send two flares to distract routers
		JammerNetzFlare flare;
		size_t bytes = 0;
		flare.serialize(sendBuffer_, bytes);
		encryptedLength = blowFish_.encrypt(sendBuffer_, bytes, MAXFRAMESIZE);
		if (encryptedLength == -1) {
			StreamLogger::instance() << "Fatal: Couldn't encrypt package, not sending to server!" << std::endl;
			return false;
		}
		for (int i = 0; i < numberOfFlares_; i++) {
			sendData(ServerInfo::serverName, ServerInfo::serverPort, sendBuffer_, encryptedLength);
		}
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

void Client::setFlareNumber(int numberOfFlares)
{
	numberOfFlares_ = numberOfFlares;
}

