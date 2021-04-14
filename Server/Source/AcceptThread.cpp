/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AcceptThread.h"

#include "BuffersConfig.h"

#include <stack>
#include "ServerLogger.h"

class PrintQualityTimer : public HighResolutionTimer {
public:
	PrintQualityTimer(TPacketStreamBundle &data) : data_(data) {
	}

	virtual void hiResTimerCallback() override
	{
		for (auto &streamData : data_) {
			if (streamData.second) {
				ServerLogger::printStatistics(4, streamData.first, streamData.second->qualityInfoPackage());
			}
		}
	}

private:
	TPacketStreamBundle &data_;
};

AcceptThread::AcceptThread(int serverPort, DatagramSocket &socket, TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue, ServerBufferConfig bufferConfig, void *keydata, int keysize)
	: Thread("ReceiverThread"), receiveSocket_(socket), incomingData_(incomingData), wakeUpQueue_(wakeUpQueue), bufferConfig_(bufferConfig), blowFish_(keydata, keysize)
{
	if (!receiveSocket_.bindToPort(serverPort)) {
		ServerLogger::deinit();
		std::cerr << "Failed to bind port to " << serverPort << std::endl;
		exit(-1);
	}
	ServerLogger::printServerStatus(("Server listening on port " + String(serverPort)).toStdString());
	
	qualityTimer_ = std::make_unique<PrintQualityTimer>(incomingData);
}

AcceptThread::~AcceptThread()
{
	qualityTimer_->stopTimer();
}

void AcceptThread::run()
{
	// Start the timer that will frequently output quality data for each of the clients' connections
	qualityTimer_->startTimer(500);
	while (!currentThreadShouldExit()) {
		switch (receiveSocket_.waitUntilReady(true, 250)) {
		case 0:
			// Timeout, nothing to be done (no data received from any client), just check if we should terminate, also wake up the MixerThread so it can do the same
			wakeUpQueue_.push(0);
			break;
		case 1: {
			// Ready to read data from socket!
			String senderIPAdress;
			int senderPortNumber;
			int dataRead = receiveSocket_.read(readbuffer, MAXFRAMESIZE, false, senderIPAdress, senderPortNumber);
			if (dataRead == -1) {
				ServerLogger::deinit();
				std::cerr << "Error reading data from socket, abort!" << std::endl;
				exit(-1);
			}

			std::string clientName = senderIPAdress.toStdString() + ":" + String(senderPortNumber).toStdString();
			if (dataRead == 0) {
				ServerLogger::printClientStatus(4, clientName, "Got empty packet from client, ignoring");
				continue;
			}
			int messageLength = blowFish_.decrypt(readbuffer, dataRead);
			if (messageLength == -1) {
				ServerLogger::printClientStatus(4, clientName, "Using wrong encryption key, can't connect");
				continue;
			}

			auto message = JammerNetzMessage::deserialize(readbuffer, messageLength);
			if (message) {
				auto audioData = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
				if (audioData) {
					// Insert this package into the right priority audio queue
					bool prefill = false;
					if (incomingData_.find(clientName) == incomingData_.end()) {
						// This is from a new client!
						ServerLogger::printClientStatus(4, clientName, "New client connected, first package received");
						incomingData_[clientName] = std::make_unique<PacketStreamQueue>(clientName);
						prefill = true;
					}
					else if (!incomingData_[clientName]) {
						ServerLogger::printClientStatus(4, clientName, "Reconnected successfully and starts sending again");
						incomingData_[clientName] = std::make_unique<PacketStreamQueue>(clientName);
						prefill = false;
					}
					if (prefill) {
						auto lastInserted = audioData;
						std::stack<std::shared_ptr<JammerNetzAudioData>> reverse;
						for (int i = 0; i < bufferConfig_.serverBufferPrefillOnConnect; i++) {
							lastInserted = lastInserted->createPrePaddingPackage();
							reverse.push(lastInserted);
						}
						while (!reverse.empty()) {
							incomingData_[clientName]->push(reverse.top());
							reverse.pop();
						}
					}

					if (incomingData_[clientName]->push(audioData)) {
						// Only if this was not a duplicate package do give the mixer thread a tick, else duplicates will cause queue drain
						wakeUpQueue_.push(1); // The value pushed is irrelevant, we just want to wake up the mixer thread which is in a blocking read on this queue
					}
				}
			}
#ifdef ALLOW_HELO
			// Useful for debugging firewall problems, use ncat and send some bytes to this port to get the message back
			else {
				// HELO
				std::string helo("HELO");
				receiveSocket_.write(senderIPAdress, senderPortNumber, helo.data(), (int)helo.size());
			}
#endif
			break;
		}
		case -1:
			ServerLogger::deinit();
			std::cerr << "Error in waitUntilReady on socket" << std::endl;
			exit(-1);
		}
	}
}