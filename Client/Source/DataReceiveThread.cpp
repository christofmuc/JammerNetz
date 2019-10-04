/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataReceiveThread.h"

#include "StreamLogger.h"

#include "ServerInfo.h"

DataReceiveThread::DataReceiveThread(DatagramSocket &socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
	: Thread("ReceiveDataFromServer"), socket_(socket), newDataHandler_(newDataHandler), currentRTT_(0.0), blowFish_(BinaryData::RandomNumbers_bin, BinaryData::RandomNumbers_binSize)
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
			int messageLength = blowFish_.decrypt(readbuffer_, dataRead);
			if (messageLength == -1) {
				StreamLogger::instance() << "Couldn't decrypt package received from server, probably fatal" << std::endl;
				continue;
			}

			// Check that the package at least seems to come from the currently active server
			if (senderIPAdress.toStdString() == ServerInfo::serverName) {
				auto message = JammerNetzMessage::deserialize(readbuffer_, messageLength);
				if (message) {
					auto audioData = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
					if (audioData) {
						// Hand off to player 
						currentRTT_ = Time::getMillisecondCounterHiRes() - audioData->timestamp();
						newDataHandler_(audioData);
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

