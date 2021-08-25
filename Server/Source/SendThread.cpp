/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SendThread.h"

#include "BuffersConfig.h"

#include "ServerLogger.h"

SendThread::SendThread(DatagramSocket& socket, TOutgoingQueue &sendQueue, TPacketStreamBundle &incomingData, void *keydata, int keysize, bool useFEC)
	: Thread("SenderThread"), sendSocket_(socket), sendQueue_(sendQueue), incomingData_(incomingData), useFEC_(useFEC)
{
	if (keydata) {
		blowFish_ = std::make_unique<BlowFish>(keydata, keysize);
	}
}

void SendThread::determineTargetIP(std::string const &targetAddress, String &ipAddress, int &portNumber) {
	ipAddress = targetAddress.substr(0, targetAddress.find(':'));
	portNumber = atoi(targetAddress.substr(targetAddress.find(':') + 1).c_str());
}

void SendThread::sendAudioBlock(std::string const &targetAddress, AudioBlock &audioBlock) {
	if (fecData_.find(targetAddress) == fecData_.end()) {
		// First time we send a package to this address, create a ring buffer!
		fecData_.emplace(targetAddress, FEC_RINGBUFFER_SIZE);
	}

	std::shared_ptr<AudioBlock> fecBlock;
	if (useFEC_ && !fecData_.find(targetAddress)->second.isEmpty()) {
		// Send FEC data
		fecBlock = fecData_.find(targetAddress)->second.getLast();
		//dataForClient.serialize(writebuffer_, bytesWritten, fecData_.find(targetAddress)->second.getLast(), SAMPLE_RATE, FEC_SAMPLERATE_REDUCTION);
	}

	JammerNetzAudioData dataForClient(audioBlock, fecBlock);
	size_t bytesWritten = 0;
	dataForClient.serialize(writebuffer_, bytesWritten);

	// Store the package sent in the FEC buffer for the next package to go out
	auto redundancyData = std::make_shared<AudioBlock>(audioBlock);
	fecData_.find(targetAddress)->second.push(redundancyData);

	String ipAddress;
	int port;
	determineTargetIP(targetAddress, ipAddress, port);

	sendWriteBuffer(ipAddress, port, bytesWritten);
}


void SendThread::sendClientInfoPackage(std::string const &targetAddress)
{
	// Loop over the incoming data streams and add them to our statistics package we are going to send to the client
	JammerNetzClientInfoMessage clientInfoPackage;
	for (auto &incoming : incomingData_) {
		if (incoming.second && incoming.second->size() > 0) {
			String ipAddress;
			int port;
			determineTargetIP(incoming.first, ipAddress, port);
			clientInfoPackage.addClientInfo(IPAddress(ipAddress), port, incoming.second->qualityInfoPackage());
		}
	}
	jassert(clientInfoPackage.getNumClients() > 0);

	size_t bytesWritten = 0;
	clientInfoPackage.serialize(writebuffer_, bytesWritten);

	String ipAddress;
	int port;
	determineTargetIP(targetAddress, ipAddress, port);
	sendWriteBuffer(ipAddress, port, bytesWritten);
}

void SendThread::sendWriteBuffer(String ipAddress, int port, size_t size) {	
	if (size <= INT_MAX) {
		int cipherLength = static_cast<int>(size);
		if (blowFish_) {
			// Encrypt in place. If no BlowFish is instantiated, it will just send unencrypted
			cipherLength = blowFish_->encrypt(writebuffer_, size, MAXFRAMESIZE);
			if (cipherLength == -1) {
				ServerLogger::deinit();
				std::cerr << "Fatal: Failed to encrypt data package, abort!" << std::endl;
				exit(-1);
			}
		}

		// Now, back to the client! This will block when not ready to send yet, but that's ok.
		sendSocket_.write(ipAddress, port, writebuffer_, cipherLength);

		ServerLogger::printServerStatistics(4, ("Packet length: " + String(cipherLength)).toStdString());
	}
}

void SendThread::run()
{
	OutgoingPackage nextBlock;
	while (!currentThreadShouldExit()) {
		// Blocking read from concurrent queue
		sendQueue_.pop(nextBlock);

		// This might be a package just to make us stop
		if (currentThreadShouldExit())
			return;

		// Now serialize the buffer and create the datagram to send back to the client
		sendAudioBlock(nextBlock.targetAddress, nextBlock.audioBlock);

		// Check if we want to send a statistics package to that client (every nth data package)
		if (packageCounters_.find(nextBlock.targetAddress) == packageCounters_.end()) {
			// First time we send a package to this address!
			packageCounters_.emplace(nextBlock.targetAddress, 0);
		}
		if (packageCounters_[nextBlock.targetAddress] % 100 == 0) {
			sendClientInfoPackage(nextBlock.targetAddress);
		}
		packageCounters_[nextBlock.targetAddress]++;
	}
}
