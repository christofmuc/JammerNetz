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

	try {
		auto payload = message->json_.at("ApplyLocalVolume");
		if (!payload.contains("target_channel_index") || !payload.contains("volume_percent")) {
			return;
		}

		auto targetChannelIndex = payload.at("target_channel_index").get<int>();
		auto remoteVolumePercent = payload.at("volume_percent").get<float>();
		if (targetChannelIndex < 0) {
			return;
		}
		remoteVolumePercent = jlimit(0.0f, 100.0f, remoteVolumePercent);

		uint32 sourceClientId = 0;
		if (payload.contains("source_client_id")) {
			sourceClientId = payload.at("source_client_id").get<uint32>();
		}

		if (payload.contains("command_sequence")) {
			auto commandSequence = payload.at("command_sequence").get<uint64_t>();
			auto key = std::make_pair(sourceClientId, static_cast<uint16>(targetChannelIndex));
			auto &lastAppliedSequence = lastAppliedRemoteVolumeSequence_[key];
			if (commandSequence <= lastAppliedSequence) {
				RemoteControlDebugLog::logEvent("client.recv",
					"drop stale ApplyLocalVolume sourceClientId=" + String(sourceClientId)
					+ " targetChannel=" + String(targetChannelIndex)
					+ " vol=" + String(remoteVolumePercent, 2)
					+ " seq=" + String(static_cast<uint64>(commandSequence))
					+ " last=" + String(static_cast<uint64>(lastAppliedSequence)));
				return;
			}
			lastAppliedSequence = commandSequence;
			RemoteControlDebugLog::logEvent("client.recv",
				"accept ApplyLocalVolume sourceClientId=" + String(sourceClientId)
				+ " targetChannel=" + String(targetChannelIndex)
				+ " vol=" + String(remoteVolumePercent, 2)
				+ " seq=" + String(static_cast<uint64>(commandSequence)));
		}
		else {
			RemoteControlDebugLog::logEvent("client.recv",
				"accept ApplyLocalVolume(no-seq) sourceClientId=" + String(sourceClientId)
				+ " targetChannel=" + String(targetChannelIndex)
				+ " vol=" + String(remoteVolumePercent, 2));
		}

		MessageManager::callAsync([targetChannelIndex, remoteVolumePercent]() {
			auto& data = Data::instance().get();
			auto inputSetup = data.getChildWithName(VALUE_INPUT_SETUP);
			auto channels = inputSetup.getChildWithName(VALUE_CHANNELS);
			if (!channels.isValid()) {
				return;
			}

			int channelCount = channels.getProperty(VALUE_CHANNEL_COUNT, 0);
			if (targetChannelIndex >= channelCount) {
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
					if (physicalIndex == targetChannelIndex) {
						activeControllerIndex = activeCount;
						break;
					}
					activeCount++;
				}
			}
			if (!activeControllerIndex.has_value()) {
				return;
			}

			auto mixer = data.getOrCreateChildWithName(VALUE_MIXER, nullptr);
			auto controllerSettings = mixer.getOrCreateChildWithName("Input" + String(*activeControllerIndex), nullptr);
			controllerSettings.setProperty(VALUE_VOLUME, remoteVolumePercent, nullptr);
		});
	} catch (const nlohmann::json::exception&) {
		// Ignore malformed control payloads
	}
}

void DataReceiveThread::run()
{
	while (!threadShouldExit()) {
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
				//TODO This is bad, when could that happen? Should probably handle more explicitly
				jassertfalse;
				std::cerr << "Error reading data from socket!" << std::endl;
				continue;
			}
			if (dataRead == 0) {
				// Weird, this seems to happen recently instead of a socket timeout even when no packets are received. So this is not a 0 byte package, but actually
				// no package at all (e.g. if you kill the server, you'll end up here instead of the socket waitUntilReady == 0)
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
							// Yes, got it. Copy it! This is thread safe if and only if the read function to the shared_ptr is atomic!
							lastClientInfoMessage_ = std::make_shared<JammerNetzClientInfoMessage>(*clientInfo);
						}
						break;
					}
                    case JammerNetzMessage::SESSIONSETUP: {
                        auto sessionInfo = std::dynamic_pointer_cast<JammerNetzSessionInfoMessage>(message);
                        if (sessionInfo) {
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
