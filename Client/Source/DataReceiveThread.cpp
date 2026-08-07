/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataReceiveThread.h"

#include "StreamLogger.h"

#include "XPlatformUtils.h"

DataReceiveThread::DataReceiveThread(DatagramSocket &socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
	: Thread("ReceiveDataFromServer"), socket_(socket), newDataHandler_(newDataHandler), currentRTT_(0.0), isReceiving_(false), receiveErrorCount_(0), currentSession_(false)
{
}

DataReceiveThread::~DataReceiveThread()
{
}

void DataReceiveThread::run()
{
	while (!threadShouldExit()) {
		try {
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
				recordReceiveError("Error reading data from socket");
				continue;
			}
			if (dataRead == 0) {
				// Weird, this seems to happen recently instead of a socket timeout even when no packets are received. So this is not a 0 byte package, but actually
				// no package at all (e.g. if you kill the server, you'll end up here instead of the socket waitUntilReady == 0)
				isReceiving_ = false;
				continue;
			}
			int messageLength = dataRead;
			{
				ScopedLock lock(blowFishLock_);
				if (blowFish_) {
					messageLength = blowFish_->decrypt(readbuffer_, safe_int_to_sizet(dataRead));
					if (messageLength == -1) {
						recordReceiveError("Could not decrypt packet received from server");
						continue;
					}
				}
			}

			// Check that the package at least seems to come from the currently active server
#ifdef SECURITY_CHECK_PACKAGE_FROM_SERVER
			if (senderIPAdress.toStdString() == ServerInfo::serverName) {
#else
			{
#endif
				auto message = JammerNetzMessage::deserialize(readbuffer_, safe_int_to_sizet(messageLength));
				if (message) {
					isReceiving_ = true;
					switch (message->getType()) {
					case JammerNetzMessage::AUDIODATA: {
						auto audioData = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
						if (audioData) {
							// Hand off to player
							currentRTT_ = Time::getMillisecondCounterHiRes() - audioData->timestamp();
							newDataHandler_(audioData);
						}
						break;
					}
					case JammerNetzMessage::CLIENTINFO: {
						auto clientInfo = std::dynamic_pointer_cast<JammerNetzClientInfoMessage>(message);
						if (clientInfo) {
							// Yes, got it. Copy it! This is thread safe if and only if the read function to the shared_ptr is atomic!
							lastClientInfoMessage_.store(std::make_shared<JammerNetzClientInfoMessage>(*clientInfo), std::memory_order_release);
						}
						break;
					}
                    case JammerNetzMessage::SESSIONSETUP: {
                        auto sessionInfo = std::dynamic_pointer_cast<JammerNetzSessionInfoMessage>(message);
                        if (sessionInfo) {
							ScopedLock sessionLock(sessionDataLock_);
                            currentSession_ = sessionInfo->channels_;
                        }
                        break;
                    }
                    case JammerNetzMessage::MessageType::GENERIC_JSON:
                        // Ignore for now
                        break;
					default:
						recordReceiveError("Received packet with an unknown message type");
					}
				}
			}
			break;
		}
		case -1:
			if (!threadShouldExit()) {
				recordReceiveError("Error waiting for data on socket");
				Thread::sleep(10);
			}
			break;
		default:
			recordReceiveError("Socket returned an unexpected readiness state");
			break;
			}
		}
		catch (const std::exception& exception) {
			recordReceiveError(exception.what());
		}
		catch (...) {
			recordReceiveError("Unknown exception while receiving a packet");
		}
	}
}

void DataReceiveThread::recordReceiveError(const char* message)
{
	const auto errorNumber = receiveErrorCount_.fetch_add(1, std::memory_order_relaxed) + 1;
	if (errorNumber == 1 || errorNumber % 100 == 0) {
		std::cerr << "JammerNetz receive error " << errorNumber << ": " << message << std::endl;
	}
}

double DataReceiveThread::currentRTT() const
{
	return currentRTT_;
}

uint64_t DataReceiveThread::receiveErrorCount() const
{
	return receiveErrorCount_.load(std::memory_order_relaxed);
}

JammerNetzChannelSetup DataReceiveThread::sessionSetup() const
{
	ScopedLock lock(sessionDataLock_);
	return currentSession_;
}

std::shared_ptr<JammerNetzClientInfoMessage> DataReceiveThread::getClientInfo() const
{
	return lastClientInfoMessage_.load(std::memory_order_acquire);
}

bool DataReceiveThread::isReceivingData() const
{
	return isReceiving_;
}

void DataReceiveThread::setCryptoKey(const void* keyData, int keyBytes) {
	ScopedLock lock(blowFishLock_);
	if (keyData) {
		blowFish_ = std::make_unique<BlowFish>(keyData, keyBytes);
	}
	else {
		// No more encryption from here on
		blowFish_.reset();
	}
}
