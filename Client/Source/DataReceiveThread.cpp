/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataReceiveThread.h"

#include "StreamLogger.h"
#include "ServerInfo.h"

DataReceiveThread::DataReceiveThread(DatagramSocket &socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
	: Thread("ReceiveDataFromServer"), socket_(socket), newDataHandler_(newDataHandler), currentRTT_(0.0), isReceiving_(false)
{
}

DataReceiveThread::~DataReceiveThread()
{
}

void DataReceiveThread::run()
{
	while (!threadShouldExit()) {
		switch (socket_.waitUntilReady(true, 500)) {
		case 0:
			// Timeout on socket, no client connected within timeout period
			isReceiving_ = false;
			break;
		case 1: {
			// Ready to read data from socket!
			String senderIPAdress;
			int senderPortNumber;
			int dataRead = socket_.read(readbuffer_, MAXFRAMESIZE, false, senderIPAdress, senderPortNumber);
			if (dataRead == -1) {
				StreamLogger::instance() << "Error reading data from socket, aborting receive thread!" << std::endl;
				return;
			}
			if (dataRead == 0) {
				// Weird, this seems to happen recently instead of a socket timeout even when no packets are received. So this is not a 0 byte package, but actually
				// no package at all (e.g. if you kill the server, you'll end up here instead of the socket waitUntilReady == 0)
				isReceiving_ = false;
				continue;
			}
			if (blowFish_) {
				ScopedLock lock(blowFishLock_);
				int messageLength = blowFish_->decrypt(readbuffer_, dataRead);
				if (messageLength == -1) {
					StreamLogger::instance() << "Couldn't decrypt package received from server, probably fatal" << std::endl;
					continue;
				}

				// Check that the package at least seems to come from the currently active server
				if (senderIPAdress.toStdString() == ServerInfo::serverName) {
					auto message = JammerNetzMessage::deserialize(readbuffer_, messageLength);
					if (message) {
						isReceiving_ = true;
						switch (message->getType()) {
						case JammerNetzMessage::AUDIODATA: {
							auto audioData = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
							if (audioData) {
								ScopedLock sessionLock(sessionDataLock_);
								// Hand off to player 
								currentRTT_ = Time::getMillisecondCounterHiRes() - audioData->timestamp();
								currentSession_ = audioData->sessionSetup(); //TODO - this is not thread safe, I trust
								newDataHandler_(audioData);
							}
							break;
						}
						case JammerNetzMessage::CLIENTINFO: {
							auto clientInfo = std::dynamic_pointer_cast<JammerNetzClientInfoMessage>(message);
							if (clientInfo) {
								// Yes, got it. Copy it! This is thread safe if and only if the read function to the shared_ptr is atomic!
								lastClientInfoMessage_ = std::make_shared<JammerNetzClientInfoMessage>(*clientInfo);
							}
							break;
						}
						default:
							// What's this?
							jassert(false);
						}
					}
				}
			}
			break;
		}
		case -1:
			StreamLogger::instance() << "Error in waitUntilReady on socket" << std::endl;
			return;
		}
	}
}

double DataReceiveThread::currentRTT() const
{
	return currentRTT_;
}

JammerNetzChannelSetup DataReceiveThread::sessionSetup() const
{
	ScopedLock lock(sessionDataLock_);
	return currentSession_;
}

std::shared_ptr<JammerNetzClientInfoMessage> DataReceiveThread::getClientInfo() const
{
	// Atomic read access to the shared ptr - the receive method might just now update it
	return std::atomic_load_explicit(&lastClientInfoMessage_, std::memory_order_acquire);
}

bool DataReceiveThread::isReceivingData() const
{
	return isReceiving_;
}

void DataReceiveThread::setCryptoKey(const void* keyData, int keyBytes) {
	ScopedLock lock(blowFishLock_);
	blowFish_ = std::make_unique<BlowFish>(keyData, keyBytes);
}
