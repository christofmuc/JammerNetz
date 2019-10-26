/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SendThread.h"

#include "BuffersConfig.h"

SendThread::SendThread(DatagramSocket& socket, TOutgoingQueue &sendQueue) 
	: Thread("SenderThread"), sendSocket_(socket), messageCounter_(0), sendQueue_(sendQueue), blowFish_(BinaryData::RandomNumbers_bin, BinaryData::RandomNumbers_binSize)
{
}

void SendThread::determineTargetIP(std::string const &targetAddress, String &ipAddress, int &portNumber) {
	ipAddress = targetAddress.substr(0, targetAddress.find(':'));
	portNumber = atoi(targetAddress.substr(targetAddress.find(':') + 1).c_str());
}

void SendThread::sendAudioBlock(std::string const &targetAddress, AudioBlock &audioBlock) {
	JammerNetzAudioData dataForClient(audioBlock);
	size_t bytesWritten;
	dataForClient.serialize(writebuffer, bytesWritten);

	if (fecData_.find(targetAddress) == fecData_.end()) {
		// First time we send a package to this address, create a ring buffer!
		fecData_.emplace(targetAddress, FEC_RINGBUFFER_SIZE);
	}
	if (!fecData_.find(targetAddress)->second.isEmpty()) {
		// Send FEC data
		dataForClient.serialize(writebuffer, bytesWritten, fecData_.find(targetAddress)->second.getLast(), SAMPLE_RATE, FEC_SAMPLERATE_REDUCTION);
	}
	// Store the package sent in the FEC buffer for the next package to go out
	auto redundancyData = std::make_shared<AudioBlock>(audioBlock);
	fecData_.find(targetAddress)->second.push(redundancyData);

	String ipAddress;
	int port;
	determineTargetIP(targetAddress, ipAddress, port);

	sendWriteBuffer(ipAddress, port, bytesWritten);
}


void SendThread::sendWriteBuffer(String ipAddress, int port, size_t size) {
	// Encrypt in place
	int cipherLength = blowFish_.encrypt(writebuffer, size, MAXFRAMESIZE);
	if (cipherLength == -1) {
		std::cerr << "Fatal: Failed to encrypt data package, abort!" << std::endl;
		exit(-1);
	}

	// Now, back to the client! This will block when not ready to send yet, but that's ok.
	sendSocket_.write(ipAddress, port, writebuffer, cipherLength);
}

void SendThread::run()
{
	OutgoingPackage nextBlock;
	while (!currentThreadShouldExit()) {
		// Blocking read from concurrent queue
		sendQueue_.pop(nextBlock);

		// Now serialize the buffer and create the datagram to send back to the client
		sendAudioBlock(nextBlock.targetAddress, nextBlock.audioBlock);
	}
}
