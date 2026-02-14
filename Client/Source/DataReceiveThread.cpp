/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataReceiveThread.h"

#include "StreamLogger.h"

#include "Data.h"
#include "Encryption.h"
#include "RemoteControlDebugLog.h"
#include "XPlatformUtils.h"

#include <optional>
#include <set>

namespace {
struct ApplyLocalVolumePayload {
	int targetChannelIndex;
	float volumePercent;
	uint32 sourceClientId;
	std::optional<uint64_t> commandSequence;
};

std::optional<ApplyLocalVolumePayload> parseApplyLocalVolumePayload(const nlohmann::json& payload)
{
	try {
		if (!payload.contains("target_channel_index") || !payload.contains("volume_percent")) {
			return {};
		}

		ApplyLocalVolumePayload parsed;
		parsed.targetChannelIndex = payload.at("target_channel_index").get<int>();
		parsed.volumePercent = jlimit(0.0f, 100.0f, payload.at("volume_percent").get<float>());
		parsed.sourceClientId = payload.contains("source_client_id") ? payload.at("source_client_id").get<uint32>() : 0;
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

DataReceiveThread::DataReceiveThread(DatagramSocket &socket, std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
	: Thread("ReceiveDataFromServer"), socket_(socket), newDataHandler_(newDataHandler), currentRTT_(0.0), isReceiving_(false), currentSession_(false)
{
	// Create listeners to get notified if the application state we depend on changes
	listeners_.push_back(std::make_unique<ValueListener>(Data::getPropertyAsValue(VALUE_CRYPTOPATH), [this](Value& value) {
		std::shared_ptr<MemoryBlock> cryptokey;
		String newCryptopath = value.getValue();
		if (newCryptopath.isNotEmpty()) {
			UDPEncryption::loadKeyfile(newCryptopath.toRawUTF8(), &cryptokey);
			if (cryptokey) {
				setCryptoKey(cryptokey->getData(), safe_sizet_to_int(cryptokey->getSize()));
			}
			else {
				// Turn off encrpytion
				setCryptoKey(nullptr, 0);
			}
		}
		else {
			// Turn off encrpytion
			setCryptoKey(nullptr, 0);
		}
	}));

	// To read the current state, just execute each of these listeners once!
	std::for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });
}

DataReceiveThread::~DataReceiveThread()
{
}

void DataReceiveThread::processControlMessage(const std::shared_ptr<JammerNetzControlMessage>& message)
{
	if (!message || !message->json_.contains("ApplyLocalVolume")) {
		return;
	}

	auto payload = parseApplyLocalVolumePayload(message->json_.at("ApplyLocalVolume"));
	if (!payload.has_value()) {
		RemoteControlDebugLog::logEvent("client.recv", "drop malformed ApplyLocalVolume payload");
		return;
	}

	if (payload->targetChannelIndex < 0) {
		return;
	}

	if (payload->commandSequence.has_value()) {
		auto key = std::make_pair(payload->sourceClientId, static_cast<uint16>(payload->targetChannelIndex));
		auto &lastAppliedSequence = lastAppliedRemoteVolumeSequence_[key];
		if (*payload->commandSequence <= lastAppliedSequence) {
			RemoteControlDebugLog::logEvent("client.recv",
				"drop stale ApplyLocalVolume sourceClientId=" + String(payload->sourceClientId)
				+ " targetChannel=" + String(payload->targetChannelIndex)
				+ " vol=" + String(payload->volumePercent, 2)
				+ " seq=" + String(static_cast<uint64>(*payload->commandSequence))
				+ " last=" + String(static_cast<uint64>(lastAppliedSequence)));
			return;
		}
		lastAppliedSequence = *payload->commandSequence;
	}

	auto& data = Data::instance().get();
	auto inputSetup = data.getChildWithName(VALUE_INPUT_SETUP);
	auto channels = inputSetup.getChildWithName(VALUE_CHANNELS);
	if (!channels.isValid()) {
		return;
	}

	int channelCount = channels.getProperty(VALUE_CHANNEL_COUNT, 0);
	if (payload->targetChannelIndex >= channelCount) {
		return;
	}

	std::optional<int> activeControllerIndex;
	int activeCount = 0;
	for (int physicalIndex = 0; physicalIndex < channelCount; physicalIndex++) {
		auto channel = channels.getChildWithName("Channel" + String(physicalIndex));
		if (!channel.isValid()) {
			continue;
		}
		bool isActive = channel.getProperty(VALUE_CHANNEL_ACTIVE, false);
		if (isActive) {
			if (physicalIndex == payload->targetChannelIndex) {
				activeControllerIndex = activeCount;
				break;
			}
			activeCount++;
		}
	}
	if (!activeControllerIndex.has_value()) {
		return;
	}

	auto resolvedControllerIndex = *activeControllerIndex;
	auto remoteVolumePercent = payload->volumePercent;
	MessageManager::callAsync([resolvedControllerIndex, remoteVolumePercent]() {
		auto& uiData = Data::instance().get();
		auto mixer = uiData.getOrCreateChildWithName(VALUE_MIXER, nullptr);
		auto controllerSettings = mixer.getOrCreateChildWithName("Input" + String(resolvedControllerIndex), nullptr);
		controllerSettings.setProperty(VALUE_VOLUME, remoteVolumePercent, nullptr);
	});
}

void DataReceiveThread::clearRemoteVolumeSequenceForClient(uint32 clientId)
{
	for (auto it = lastAppliedRemoteVolumeSequence_.begin(); it != lastAppliedRemoteVolumeSequence_.end(); ) {
		if (it->first.first == clientId) {
			it = lastAppliedRemoteVolumeSequence_.erase(it);
		}
		else {
			++it;
		}
	}
}

void DataReceiveThread::clearAllRemoteSequencesOnSessionReset()
{
	lastAppliedRemoteVolumeSequence_.clear();
}

void DataReceiveThread::handleSessionSetupChange(const JammerNetzChannelSetup& newSessionSetup)
{
	std::set<uint32> previousClientIds;
	for (const auto& channel : currentSession_.channels) {
		if (channel.sourceClientId != 0) {
			previousClientIds.insert(channel.sourceClientId);
		}
	}

	std::set<uint32> currentClientIds;
	for (const auto& channel : newSessionSetup.channels) {
		if (channel.sourceClientId != 0) {
			currentClientIds.insert(channel.sourceClientId);
		}
	}

	for (auto clientId : previousClientIds) {
		if (currentClientIds.find(clientId) == currentClientIds.end()) {
			clearRemoteVolumeSequenceForClient(clientId);
		}
	}

	bool topologyChanged = currentSession_.channels.size() != newSessionSetup.channels.size();
	if (!topologyChanged) {
		for (size_t i = 0; i < currentSession_.channels.size(); i++) {
			const auto& oldChannel = currentSession_.channels[i];
			const auto& newChannel = newSessionSetup.channels[i];
			if (oldChannel.sourceClientId != newChannel.sourceClientId
				|| oldChannel.sourceChannelIndex != newChannel.sourceChannelIndex) {
				topologyChanged = true;
				break;
			}
		}
	}

	if (topologyChanged) {
		for (auto clientId : currentClientIds) {
			clearRemoteVolumeSequenceForClient(clientId);
		}
	}
}

void DataReceiveThread::run()
{
	while (!threadShouldExit()) {
		switch (socket_.waitUntilReady(true, 500)) {
		case 0:
			// Timeout on socket, no client connected within timeout period
			if (isReceiving_) {
				clearAllRemoteSequencesOnSessionReset();
			}
			isReceiving_ = false;
			break;
		case 1: {
			// Ready to read data from socket!
			String senderIPAdress;
			int senderPortNumber;
			int dataRead = socket_.read(readbuffer_, MAXFRAMESIZE, false, senderIPAdress, senderPortNumber);
			if (dataRead == -1) {
				//TODO This is bad, when could that happen? Should probably handle more explicitly
				jassertfalse;
				std::cerr << "Error reading data from socket!" << std::endl;
				continue;
			}
			if (dataRead == 0) {
				// Weird, this seems to happen recently instead of a socket timeout even when no packets are received. So this is not a 0 byte package, but actually
				// no package at all (e.g. if you kill the server, you'll end up here instead of the socket waitUntilReady == 0)
				if (isReceiving_) {
					clearAllRemoteSequencesOnSessionReset();
				}
				isReceiving_ = false;
				continue;
			}
			int messageLength = -1;
			if (blowFish_) {
				ScopedLock lock(blowFishLock_);
				messageLength = blowFish_->decrypt(readbuffer_, safe_int_to_sizet(dataRead));
				if (messageLength == -1) {
					//TODO - This should be handled differently
					std::cerr << "Couldn't decrypt package received from server, probably fatal" << std::endl;
					continue;
				}
			}
			else {
				// For now, accept unencrypted data as well, and hope for the best
				messageLength = dataRead;
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
							ScopedLock sessionLock(sessionDataLock_);
							// Hand off to player
							currentRTT_ = Time::getMillisecondCounterHiRes() - audioData->timestamp();
							newDataHandler_(audioData);
						}
						break;
					}
					case JammerNetzMessage::CLIENTINFO: {
						auto clientInfo = std::dynamic_pointer_cast<JammerNetzClientInfoMessage>(message);
						if (clientInfo) {
							if (lastClientInfoMessage_ && clientInfo->getNumClients() < lastClientInfoMessage_->getNumClients()) {
								clearAllRemoteSequencesOnSessionReset();
							}
							// Yes, got it. Copy it! This is thread safe if and only if the read function to the shared_ptr is atomic!
							lastClientInfoMessage_ = std::make_shared<JammerNetzClientInfoMessage>(*clientInfo);
						}
						break;
					}
                    case JammerNetzMessage::SESSIONSETUP: {
                        auto sessionInfo = std::dynamic_pointer_cast<JammerNetzSessionInfoMessage>(message);
                        if (sessionInfo) {
							handleSessionSetupChange(sessionInfo->channels_);
                            currentSession_ = sessionInfo->channels_; //TODO - this is not thread safe, I trust
                        }
                        break;
                    }
                    case JammerNetzMessage::MessageType::GENERIC_JSON:
                        processControlMessage(std::dynamic_pointer_cast<JammerNetzControlMessage>(message));
                        break;
					default:
						// What's this?
						jassert(false);
					}
				}
			}
			break;
		}
		case -1:
			//TODO - when does this happen? This could kill the receive thread and the client would need to be restartet
			std::cerr << "Error in waitUntilReady on socket" << std::endl;
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
	return lastClientInfoMessage_;
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
