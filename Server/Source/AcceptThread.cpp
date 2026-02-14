/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AcceptThread.h"

#include "BuffersConfig.h"
#include "RemoteControlDebugLog.h"
#include "XPlatformUtils.h"

#include <stack>
#include <utility>
#include "ServerLogger.h"
#include <optional>

namespace {
struct SetRemoteVolumePayload {
	uint32 targetClientId;
	uint16 targetChannelIndex;
	float volumePercent;
	std::optional<uint64_t> commandSequence;
};

std::optional<SetRemoteVolumePayload> parseSetRemoteVolumePayload(const nlohmann::json& payload)
{
	try {
		if (!payload.contains("target_client_id") || !payload.contains("target_channel_index") || !payload.contains("volume_percent")) {
			return {};
		}

		SetRemoteVolumePayload parsed;
		parsed.targetClientId = payload.at("target_client_id").get<uint32>();
		parsed.targetChannelIndex = payload.at("target_channel_index").get<uint16>();
		parsed.volumePercent = jlimit(0.0f, 100.0f, payload.at("volume_percent").get<float>());
		if (payload.contains("command_sequence")) {
			parsed.commandSequence = payload.at("command_sequence").get<uint64_t>();
		}
		return parsed;
	}
	catch (const nlohmann::json::exception&) {
		return {};
	}
}
}

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

AcceptThread::AcceptThread(int serverPort, DatagramSocket &socket, TPacketStreamBundle &incomingData, TMessageQueue &wakeUpQueue, ServerBufferConfig bufferConfig, void *keydata, int keysize, ValueTree serverConfiguration, ClientIdentityRegistry &clientIdentityRegistry, std::atomic<uint64_t> &sessionControlRevision)
	: Thread("ReceiverThread")
	    , receiveSocket_(socket)
	    , incomingData_(incomingData)
	    , wakeUpQueue_(wakeUpQueue)
	    , serverConfiguration_(serverConfiguration)
	    , clientIdentityRegistry_(clientIdentityRegistry)
	    , sessionControlRevision_(sessionControlRevision)
	    , bufferConfig_(bufferConfig)
{
	if (keydata) {
		blowFish_ = std::make_unique<BlowFish>(keydata, keysize);
	}

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

bool AcceptThread::sendControlMessageToClient(const std::string& targetAddress, nlohmann::json payload)
{
	auto separator = targetAddress.find(':');
	if (separator == std::string::npos) {
		return false;
	}

	String ipAddress(targetAddress.substr(0, separator));
	int port = atoi(targetAddress.substr(separator + 1).c_str());
	if (port <= 0) {
		return false;
	}

	uint8 buffer[MAXFRAMESIZE];
	JammerNetzControlMessage controlMessage(payload);
	size_t bytesWritten = 0;
	controlMessage.serialize(buffer, bytesWritten);
	if (bytesWritten == 0) {
		return false;
	}

	if (!sizet_is_safe_as_int(bytesWritten)) {
		return false;
	}

	int bytesToSend = static_cast<int>(bytesWritten);
	if (blowFish_) {
		bytesToSend = blowFish_->encrypt(buffer, bytesWritten, MAXFRAMESIZE);
		if (bytesToSend == -1) {
			return false;
		}
	}

	// Never block control forwarding in the receive thread; drop if socket is currently not writable.
	if (receiveSocket_.waitUntilReady(false, 0) != 1) {
		return false;
	}

	return receiveSocket_.write(ipAddress, port, buffer, bytesToSend) > 0;
}

void AcceptThread::processControlMessage(std::shared_ptr<JammerNetzControlMessage> message, std::string const& clientName)
{
	if (!message) {
		return;
	}

	if (message->json_.contains("FEC")) {
		serverConfiguration_.setProperty("FEC", message->json_["FEC"].operator bool(), nullptr);
	}

	if (!message->json_.contains("SetRemoteVolume")) {
		return;
	}

	auto parsedPayload = parseSetRemoteVolumePayload(message->json_.at("SetRemoteVolume"));
	if (!parsedPayload.has_value()) {
		RemoteControlDebugLog::logEvent("server.accept", "drop malformed SetRemoteVolume payload");
		return;
	}

	auto sourceClientId = clientIdentityRegistry_.getOrAssignClientId(clientName);
	if (parsedPayload->commandSequence.has_value()) {
		auto sequenceKey = std::make_tuple(sourceClientId, parsedPayload->targetClientId, parsedPayload->targetChannelIndex);
		auto found = lastForwardedControlSequence_.find(sequenceKey);
		if (found != lastForwardedControlSequence_.end()) {
			if (*parsedPayload->commandSequence <= found->second) {
				RemoteControlDebugLog::logEvent("server.accept",
					"drop duplicate/out-of-order SetRemoteVolume srcClientId=" + String(sourceClientId)
					+ " targetClientId=" + String(parsedPayload->targetClientId)
					+ " targetChannel=" + String(parsedPayload->targetChannelIndex)
					+ " seq=" + String(static_cast<uint64>(*parsedPayload->commandSequence))
					+ " last=" + String(static_cast<uint64>(found->second)));
				return;
			}
			found->second = *parsedPayload->commandSequence;
		}
		else {
			lastForwardedControlSequence_.emplace(sequenceKey, *parsedPayload->commandSequence);
		}
	}

	auto targetEndpoint = clientIdentityRegistry_.endpointForClientId(parsedPayload->targetClientId);
	if (!targetEndpoint.has_value()) {
		auto sequenceText = parsedPayload->commandSequence.has_value() ? String(static_cast<uint64>(*parsedPayload->commandSequence)) : String("none");
		RemoteControlDebugLog::logEvent("server.accept",
			"drop SetRemoteVolume unresolved targetClientId=" + String(parsedPayload->targetClientId)
			+ " seq=" + sequenceText);
		return;
	}

	nlohmann::json forwardedControl;
	forwardedControl["ApplyLocalVolume"] = {
		{ "target_channel_index", parsedPayload->targetChannelIndex },
		{ "volume_percent", parsedPayload->volumePercent },
		{ "source_client_id", sourceClientId },
	};
	if (parsedPayload->commandSequence.has_value()) {
		forwardedControl["ApplyLocalVolume"]["command_sequence"] = *parsedPayload->commandSequence;
	}
	bool sent = sendControlMessageToClient(*targetEndpoint, forwardedControl);
	if (!sent) {
		RemoteControlDebugLog::logEvent("server.accept",
			"drop ApplyLocalVolume targetClientId=" + String(parsedPayload->targetClientId)
			+ " targetChannel=" + String(parsedPayload->targetChannelIndex)
			+ " reason=socket-not-writable");
		return;
	}

	// Throttle session revision bumps to avoid flooding SESSIONSETUP while dragging sliders.
	constexpr juce::int64 kSessionRevisionMinIntervalMs = 60;
	auto now = Time::currentTimeMillis();
	if (lastSessionRevisionBumpMillis_ == 0 || (now - lastSessionRevisionBumpMillis_) >= kSessionRevisionMinIntervalMs) {
		sessionControlRevision_.fetch_add(1, std::memory_order_relaxed);
		lastSessionRevisionBumpMillis_ = now;
	}
}

void AcceptThread::processAudioMessage(std::shared_ptr<JammerNetzAudioData> audioData, std::string const& clientName)
{
    if (audioData) {
		clientIdentityRegistry_.getOrAssignClientId(clientName);
        // Insert this package into the right priority audio queue
        bool prefill = false;
        if (incomingData_.find(clientName) == incomingData_.end()) {
            // This is from a new client!
            ServerLogger::printClientStatus(4, clientName,
                                            "New client connected, first package received");
            incomingData_[clientName] = std::make_unique<PacketStreamQueue>(clientName);
            prefill = true;
        } else if (!incomingData_[clientName]) {
            ServerLogger::printClientStatus(4, clientName,
                                            "Reconnected successfully and starts sending again");
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

			std::string clientName = senderIPAdress.toStdString() + ":" + String(senderPortNumber).toStdString();
			if (dataRead == 0) {
				ServerLogger::printClientStatus(4, clientName, "Got empty packet from client, ignoring");
				continue;
			}
			int messageLength = -1;
			if (blowFish_ && dataRead > 0) {
				messageLength = blowFish_->decrypt(readbuffer, (size_t) dataRead);
				if (messageLength == -1) {
					ServerLogger::printClientStatus(4, clientName, "Using wrong encryption key, can't connect");
					continue;
				}
			}
			else {
				// No encryption!
				messageLength = dataRead;
			}

            if (messageLength > 0) {
                auto message = JammerNetzMessage::deserialize(readbuffer, (size_t) messageLength);
                if (message) {
                    switch (message->getType()) {
                        case JammerNetzMessage::MessageType::AUDIODATA:
                            processAudioMessage(std::dynamic_pointer_cast<JammerNetzAudioData>(message), clientName);
                            break;
                        case JammerNetzMessage::MessageType::GENERIC_JSON:
                            processControlMessage(std::dynamic_pointer_cast<JammerNetzControlMessage>(message), clientName);
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
