/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SendThread.h"

#include "BuffersConfig.h"
#include "XPlatformUtils.h"
#include "ServerLogger.h"

SendThread::SendThread(DatagramSocket& socket, CriticalSection& socketWriteLock,
	TOutgoingQueue &sendQueue, TPacketStreamBundle &incomingData,
	TPeerEndpointMap &peerEndpoints,
	std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer,
	ValueTree serverConfiguration)
	: Thread("SenderThread")
    , sendQueue_(sendQueue)
    , incomingData_(incomingData)
	, peerEndpoints_(peerEndpoints)
    , sendSocket_(socket)
	, socketWriteLock_(socketWriteLock)
	, serverConfiguration_(serverConfiguration)
	, serverSealer_(std::move(serverSealer))
{
}

bool SendThread::determineTargetIP(std::string const &peerId, String &ipAddress, int &portNumber) const {
	const auto endpoint = peerEndpoints_.find(peerId);
	if (endpoint == peerEndpoints_.end() || !endpoint->second) {
		return false;
	}
	const auto snapshot = endpoint->second->snapshot();
	ipAddress = snapshot.first;
	portNumber = snapshot.second;
	return ipAddress.isNotEmpty() && portNumber > 0;
}

void SendThread::sendAudioBlock(OutgoingPackage const &package) {
	const auto &targetAddress = package.targetAddress;
	if (fecData_.find(targetAddress) == fecData_.end()) {
		// First time we send a package to this address, create a ring buffer!
		fecData_.emplace(targetAddress, FEC_RINGBUFFER_SIZE);
	}

	std::shared_ptr<AudioBlock> fecBlock;
    bool useFEC = serverConfiguration_.getProperty("FEC").operator bool();
	if (useFEC && !fecData_.find(targetAddress)->second.isEmpty()) {
		// Send FEC data
		fecBlock = fecData_.find(targetAddress)->second.getLast();
		//dataForClient.serialize(writebuffer_, bytesWritten, fecData_.find(targetAddress)->second.getLast(), SAMPLE_RATE, FEC_SAMPLERATE_REDUCTION);
	}

	JammerNetzAudioData dataForClient(package.audioBlock, fecBlock);
	if (!JammerNetzProtocol::supportsSplitSessionInfo(package.receiverProtocolVersion)) {
		dataForClient.setLegacySessionSetup(package.sessionSetup);
	}
	size_t bytesWritten = 0;
	dataForClient.serialize(writebuffer_, bytesWritten);

	// Store the package sent in the FEC buffer for the next package to go out
	auto redundancyData = std::make_shared<AudioBlock>(package.audioBlock);
	fecData_.find(targetAddress)->second.push(redundancyData);

	sendWriteBuffer(targetAddress, bytesWritten);
}


void SendThread::sendClientInfoPackage(std::string const &targetAddress)
{
	// Loop over the incoming data streams and add them to our statistics package we are going to send to the client
	JammerNetzClientInfoMessage clientInfoPackage;
	clientInfoPackage.addCapability(JammerNetzCapability::MtuProbeV1);
	for (auto &incoming : incomingData_) {
		JammerNetzStreamQualityInfo qualityInfo;
		if (incoming.second && incoming.second->snapshot().size > 0 && incoming.second->qualityInfo(qualityInfo)) {
			String ipAddress;
			int port = 0;
			if (determineTargetIP(incoming.first, ipAddress, port)) {
				clientInfoPackage.addClientInfo(IPAddress(ipAddress), port, qualityInfo);
			}
		}
	}
	if (clientInfoPackage.getNumClients() == 0) {
		return;
	}

	size_t bytesWritten = 0;
	clientInfoPackage.serialize(writebuffer_, bytesWritten);

	sendWriteBuffer(targetAddress, bytesWritten);
}

void SendThread::sendSessionInfoPackage(std::string const &targetAddress, JammerNetzChannelSetup &sessionSetup)
{
    // Loop over the incoming data streams and add them to our statistics package we are going to send to the client
        JammerNetzSessionInfoMessage sessionInfoMessage;
    sessionInfoMessage.channels_.channels = sessionSetup.channels;
	sessionInfoMessage.addCapability(JammerNetzCapability::MtuProbeV1);

    size_t bytesWritten = 0;
    sessionInfoMessage.serialize(writebuffer_, bytesWritten);

    sendWriteBuffer(targetAddress, bytesWritten);
}

void SendThread::sendWriteBuffer(std::string const &peerId, size_t size) {
	String ipAddress;
	int port = 0;
	if (!determineTargetIP(peerId, ipAddress, port)) {
		return;
	}
	const auto sealed = serverSealer_->seal(
		std::span<const uint8>(writebuffer_, size), std::span<uint8>(wireBuffer_));
	if (sealed && sizet_is_safe_as_int(sealed.bytesWritten)) {
		const int cipherLength = static_cast<int>(sealed.bytesWritten);

		// Now, back to the client! This will block when not ready to send yet, but that's ok.
		{
			const ScopedLock socketLock(socketWriteLock_);
			sendSocket_.write(ipAddress, port, writebuffer_, cipherLength);
		}

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
		sendAudioBlock(nextBlock);

		// Check if we want to send a statistics package to that client (every nth data package)
		if (packageCounters_.find(nextBlock.targetAddress) == packageCounters_.end()) {
			// First time we send a package to this address!
			packageCounters_.emplace(nextBlock.targetAddress, 0);
		}
		if (packageCounters_[nextBlock.targetAddress] % 100 == 0) {
			sendClientInfoPackage(nextBlock.targetAddress);
			if (JammerNetzProtocol::supportsSplitSessionInfo(nextBlock.receiverProtocolVersion)) {
				sendSessionInfoPackage(nextBlock.targetAddress, nextBlock.sessionSetup);
			}
		}
		packageCounters_[nextBlock.targetAddress]++;
	}
}
