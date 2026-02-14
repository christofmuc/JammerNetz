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

void AcceptThread::sendControlMessageToClient(const std::string& targetAddress, nlohmann::json payload)
{
	auto separator = targetAddress.find(':');
	if (separator == std::string::npos) {
		return;
	}

	String ipAddress(targetAddress.substr(0, separator));
	int port = atoi(targetAddress.substr(separator + 1).c_str());
	if (port <= 0) {
		return;
	}

	uint8 buffer[MAXFRAMESIZE];
	JammerNetzControlMessage controlMessage(payload);
	size_t bytesWritten = 0;
	controlMessage.serialize(buffer, bytesWritten);
	if (bytesWritten == 0) {
		return;
	}

	if (!sizet_is_safe_as_int(bytesWritten)) {
		return;
	}

	int bytesToSend = static_cast<int>(bytesWritten);
	if (blowFish_) {
		bytesToSend = blowFish_->encrypt(buffer, bytesWritten, MAXFRAMESIZE);
		if (bytesToSend == -1) {
			return;
		}
	}

	receiveSocket_.write(ipAddress, port, buffer, bytesToSend);
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

	try {
		auto payload = message->json_.at("SetRemoteVolume");
		if (!payload.contains("target_client_id") || !payload.contains("target_channel_index") || !payload.contains("volume_percent")) {
			return;
		}

		auto targetClientId = payload.at("target_client_id").get<uint32>();
		auto targetChannelIndex = payload.at("target_channel_index").get<uint16>();
		auto volumePercent = payload.at("volume_percent").get<float>();
		volumePercent = jlimit(0.0f, 100.0f, volumePercent);
		auto sourceClientId = clientIdentityRegistry_.getOrAssignClientId(clientName);

		uint64_t commandSequence = 0;
		bool hasCommandSequence = false;
		if (payload.contains("command_sequence")) {
			commandSequence = payload.at("command_sequence").get<uint64_t>();
			hasCommandSequence = true;
		}
		if (hasCommandSequence) {
			auto sequenceKey = std::make_tuple(sourceClientId, targetClientId, targetChannelIndex);
			auto &lastSequence = lastForwardedControlSequence_[sequenceKey];
			if (commandSequence <= lastSequence) {
				RemoteControlDebugLog::logEvent("server.accept",
					"drop duplicate/out-of-order SetRemoteVolume srcClientId=" + String(sourceClientId)
					+ " targetClientId=" + String(targetClientId)
					+ " targetChannel=" + String(targetChannelIndex)
					+ " seq=" + String(static_cast<uint64>(commandSequence))
					+ " last=" + String(static_cast<uint64>(lastSequence)));
				return;
			}
			lastSequence = commandSequence;
		}
		auto sequenceText = hasCommandSequence ? String(static_cast<uint64>(commandSequence)) : String("none");
		RemoteControlDebugLog::logEvent("server.accept",
			"recv SetRemoteVolume from=" + String(clientName.c_str())
			+ " srcClientId=" + String(sourceClientId)
			+ " targetClientId=" + String(targetClientId)
			+ " targetChannel=" + String(targetChannelIndex)
			+ " vol=" + String(volumePercent, 2)
			+ " seq=" + sequenceText);

		auto targetEndpoint = clientIdentityRegistry_.endpointForClientId(targetClientId);
		if (!targetEndpoint.has_value()) {
			RemoteControlDebugLog::logEvent("server.accept",
				"drop SetRemoteVolume unresolved targetClientId=" + String(targetClientId)
				+ " seq=" + sequenceText);
			return;
		}

		nlohmann::json forwardedControl;
		forwardedControl["ApplyLocalVolume"] = {
			{ "target_channel_index", targetChannelIndex },
			{ "volume_percent", volumePercent },
			{ "source_client_id", sourceClientId },
		};
		if (hasCommandSequence) {
			forwardedControl["ApplyLocalVolume"]["command_sequence"] = commandSequence;
		}
		sendControlMessageToClient(*targetEndpoint, forwardedControl);
		RemoteControlDebugLog::logEvent("server.accept",
			"forward ApplyLocalVolume to=" + String(targetEndpoint->c_str())
			+ " targetChannel=" + String(targetChannelIndex)
			+ " vol=" + String(volumePercent, 2)
			+ " seq=" + sequenceText
			+ " copies=1");
		sessionControlRevision_.fetch_add(1, std::memory_order_relaxed);
	} catch (const nlohmann::json::exception&) {
		// Ignore malformed control messages
		RemoteControlDebugLog::logEvent("server.accept", "drop malformed SetRemoteVolume payload");
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
