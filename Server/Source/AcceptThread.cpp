/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AcceptThread.h"

#include "BuffersConfig.h"
#include "XPlatformUtils.h"

#include <algorithm>
#include "ServerLogger.h"

class PrintQualityTimer : public HighResolutionTimer {
public:
	PrintQualityTimer(TPacketStreamBundle &data) : data_(data) {
	}

	virtual void hiResTimerCallback() override
	{
		for (auto &streamData : data_) {
			JammerNetzStreamQualityInfo qualityInfo;
			if (streamData.second && streamData.second->qualityInfo(qualityInfo)) {
				ServerLogger::printStatistics(4, streamData.first, qualityInfo);
			}
		}
	}

private:
	TPacketStreamBundle &data_;
};

AcceptThread::AcceptThread(int serverPort, DatagramSocket &socket, CriticalSection& socketWriteLock,
	TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue, TPeerEndpointMap &peerEndpoints,
	ServerBufferConfig bufferConfig, std::shared_ptr<const JammerNetzSecure::SessionKey> sessionKey,
	std::shared_ptr<JammerNetzSecure::SecureDatagramSealer> serverSealer, ValueTree serverConfiguration)
	: Thread("ReceiverThread")
    , receiveSocket_(socket)
	, socketWriteLock_(socketWriteLock)
    , incomingData_(incomingData)
	, peerEndpoints_(peerEndpoints)
    , wakeUpQueue_(wakeUpQueue)
    , serverConfiguration_(serverConfiguration)
	, bufferConfig_(bufferConfig)
	, opener_(std::move(sessionKey), JammerNetzSecure::Direction::ClientToServer)
	, serverSealer_(std::move(serverSealer))
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

void AcceptThread::processControlMessage(std::shared_ptr<JammerNetzControlMessage> message,
	const String& senderIPAddress, int senderPort, int receivedPayloadBytes)
{
    if (message)
    {
        if (message->json_.contains("FEC")) {
            serverConfiguration_.setProperty("FEC", message->json_["FEC"].operator bool(), nullptr);
        }
		if (message->json_.contains("mtu_probe_v1")) {
			const auto& probe = message->json_["mtu_probe_v1"];
			if (probe.is_object() && probe.contains("id") && probe["id"].is_number_unsigned()
				&& probe.contains("size") && probe["size"].is_number_integer()
				&& probe["size"] == receivedPayloadBytes) {
				sendMtuAcknowledgement(senderIPAddress, senderPort,
					probe["id"].get<uint64>(), receivedPayloadBytes);
			}
		}
    }
}

void AcceptThread::sendMtuAcknowledgement(const String& senderIPAddress, int senderPort,
	uint64 probeId, int receivedPayloadBytes)
{
	nlohmann::json acknowledgement;
	acknowledgement["mtu_ack_v1"]["id"] = probeId;
	acknowledgement["mtu_ack_v1"]["size"] = receivedPayloadBytes;
	JammerNetzControlMessage response(acknowledgement);
	size_t bytesWritten = 0;
	response.serialize(plaintextBuffer_, bytesWritten);
	const auto sealed = serverSealer_->seal(
		std::span<const uint8>(plaintextBuffer_, bytesWritten), std::span<uint8>(wireBuffer_));
	if (sealed && sizet_is_safe_as_int(sealed.bytesWritten)) {
		const ScopedLock socketLock(socketWriteLock_);
		receiveSocket_.write(senderIPAddress, senderPort, wireBuffer_, static_cast<int>(sealed.bytesWritten));
	}
}

void AcceptThread::processAudioMessage(std::shared_ptr<JammerNetzAudioData> audioData, std::string const& clientName)
{
    if (audioData) {
		// Publish a fully constructed, stable value. Concurrent readers never observe
		// an empty mapped smart pointer and never access queue ownership directly.
		auto insertion = incomingData_.insert(
			std::make_pair(clientName, std::make_shared<ClientState>(clientName)));
		auto clientState = insertion.first->second;
		const auto prefillCount = static_cast<std::size_t>(
			std::max(0, bufferConfig_.serverBufferPrefillOnConnect));
		const auto result = clientState->push(audioData, prefillCount);

		switch (result.transition) {
		case ClientConnectionTransition::InitialConnection:
			ServerLogger::printClientStatus(4, clientName, "New client connected, first package received");
			break;
		case ClientConnectionTransition::GraceRecovery:
			ServerLogger::printClientStatus(4, clientName, "Client recovered during disconnect grace period");
			break;
		case ClientConnectionTransition::Reconnection:
			ServerLogger::printClientStatus(4, clientName, "Reconnected successfully and starts sending again");
			break;
		case ClientConnectionTransition::None:
			break;
		}

		if (result.queued) {
			// Only if this was not a duplicate package do give the mixer thread a tick, else duplicates will cause queue drain
			wakeUpQueue_.push(
                    1); // The value pushed is irrelevant, we just want to wake up the mixer thread which is in a blocking read on this queue
        }
    }
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

			if (dataRead == 0) {
				continue;
			}
			const auto opened = opener_.open(
				std::span<const uint8>(readbuffer, safe_int_to_sizet(dataRead)), std::span<uint8>(plaintextBuffer_));
			if (!opened || !sizet_is_safe_as_int(opened.bytesWritten)) {
				// Invalid network traffic is deliberately silent and creates no peer state.
				const auto rejected = rejectedDatagrams_.fetch_add(1, std::memory_order_relaxed) + 1;
				if (rejected % 1000 == 0) {
					ServerLogger::printServerStatus("Rejected " + std::to_string(rejected)
						+ " unauthenticated, malformed, or replayed datagrams");
				}
				continue;
			}
			const int messageLength = static_cast<int>(opened.bytesWritten);
			const std::string clientName = JammerNetzSecure::senderInstanceIdString(opened.metadata.senderInstanceId);
			auto endpointInsertion = peerEndpoints_.insert(
				std::make_pair(clientName, std::make_shared<PeerEndpoint>()));
			if (opened.metadata.advancedHighWatermark) {
				endpointInsertion.first->second->updateIfNewer(
					senderIPAdress, senderPortNumber, opened.metadata.securityCounter);
			}

            if (messageLength > 0) {
                auto message = JammerNetzMessage::deserialize(plaintextBuffer_, (size_t) messageLength);
                if (message) {
                    switch (message->getType()) {
                        case JammerNetzMessage::MessageType::AUDIODATA:
                            processAudioMessage(std::dynamic_pointer_cast<JammerNetzAudioData>(message), clientName);
                            break;
                        case JammerNetzMessage::MessageType::GENERIC_JSON:
                            processControlMessage(std::dynamic_pointer_cast<JammerNetzControlMessage>(message),
								senderIPAdress, senderPortNumber, dataRead);
                            break;
                        case JammerNetzMessage::MessageType::CLIENTINFO:
                            // fall through
                        case JammerNetzMessage::MessageType::SESSIONSETUP:
                            // fall through
                        default:
                            // Ignoring Message
                            break;
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
            }
			break;
		}
		case -1:
			ServerLogger::deinit();
			std::cerr << "Error in waitUntilReady on socket" << std::endl;
			exit(-1);
		}
	}
}
