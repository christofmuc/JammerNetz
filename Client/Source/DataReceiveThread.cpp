/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataReceiveThread.h"

#include "StreamLogger.h"

#include "XPlatformUtils.h"

#include <limits>

DataReceiveThread::DataReceiveThread(DatagramSocket &socket,
	std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler,
	std::function<void(bool)> mtuCapabilityHandler,
	std::function<void(uint64, int)> mtuAcknowledgementHandler)
	: Thread("ReceiveDataFromServer"), socket_(socket), newDataHandler_(newDataHandler),
	mtuCapabilityHandler_(std::move(mtuCapabilityHandler)),
	mtuAcknowledgementHandler_(std::move(mtuAcknowledgementHandler)),
	currentRTT_(0.0), isReceiving_(false), receiveErrorCount_(0), currentSession_(false)
{
}

DataReceiveThread::~DataReceiveThread()
{
}

void DataReceiveThread::run()
{
	while (!threadShouldExit()) {
		try {
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
				recordReceiveError("Error reading data from socket");
				continue;
			}
			if (dataRead == 0) {
				// Weird, this seems to happen recently instead of a socket timeout even when no packets are received. So this is not a 0 byte package, but actually
				// no package at all (e.g. if you kill the server, you'll end up here instead of the socket waitUntilReady == 0)
				isReceiving_ = false;
				continue;
			}
			int messageLength = -1;
			{
				ScopedLock lock(cryptoLock_);
				if (!opener_) {
					recordReceiveError("No session key loaded");
					continue;
				}
				const auto opened = opener_->open(
					std::span<const uint8>(readbuffer_, safe_int_to_sizet(dataRead)),
					std::span<uint8>(plaintextBuffer_));
				if (!opened || !sizet_is_safe_as_int(opened.bytesWritten)) {
					recordReceiveError("Rejected unauthenticated or replayed packet");
					continue;
				}
				messageLength = static_cast<int>(opened.bytesWritten);
			}

			// Check that the package at least seems to come from the currently active server
#ifdef SECURITY_CHECK_PACKAGE_FROM_SERVER
			if (senderIPAdress.toStdString() == ServerInfo::serverName) {
#else
			{
#endif
				auto message = JammerNetzMessage::deserialize(plaintextBuffer_, safe_int_to_sizet(messageLength));
				if (message) {
					isReceiving_ = true;
					switch (message->getType()) {
					case JammerNetzMessage::AUDIODATA: {
						auto audioData = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
						if (audioData) {
							if (!JammerNetzProtocol::supportsSplitSessionInfo(audioData->protocolVersion())) {
								if (const auto legacySession = audioData->legacySessionSetup()) {
									ScopedLock sessionLock(sessionDataLock_);
									currentSession_ = *legacySession;
								}
							}
							// Hand off to player
							currentRTT_ = Time::getMillisecondCounterHiRes() - audioData->timestamp();
							newDataHandler_(audioData);
						}
						break;
					}
					case JammerNetzMessage::CLIENTINFO: {
						auto clientInfo = std::dynamic_pointer_cast<JammerNetzClientInfoMessage>(message);
						if (clientInfo) {
							if (mtuCapabilityHandler_) {
								mtuCapabilityHandler_(clientInfo->supportsCapability(JammerNetzCapability::MtuProbeV1));
							}
							// Yes, got it. Copy it! This is thread safe if and only if the read function to the shared_ptr is atomic!
							lastClientInfoMessage_.store(std::make_shared<JammerNetzClientInfoMessage>(*clientInfo), std::memory_order_release);
						}
						break;
					}
                    case JammerNetzMessage::SESSIONSETUP: {
                        auto sessionInfo = std::dynamic_pointer_cast<JammerNetzSessionInfoMessage>(message);
                        if (sessionInfo) {
							if (mtuCapabilityHandler_) {
								mtuCapabilityHandler_(sessionInfo->supportsCapability(JammerNetzCapability::MtuProbeV1));
							}
							ScopedLock sessionLock(sessionDataLock_);
                            currentSession_ = sessionInfo->channels_;
                        }
                        break;
                    }
                    case JammerNetzMessage::MessageType::GENERIC_JSON: {
						auto control = std::dynamic_pointer_cast<JammerNetzControlMessage>(message);
						if (control && control->json_.contains("mtu_ack_v1")) {
							const auto& acknowledgement = control->json_["mtu_ack_v1"];
							if (acknowledgement.is_object() && acknowledgement.contains("id")
								&& acknowledgement["id"].is_number_unsigned()
								&& acknowledgement.contains("size") && acknowledgement["size"].is_number_integer()
								&& mtuAcknowledgementHandler_) {
								const auto payloadBytes = acknowledgement["size"].get<int64_t>();
								if (payloadBytes > 0 && payloadBytes <= std::numeric_limits<int>::max()) {
									mtuAcknowledgementHandler_(acknowledgement["id"].get<uint64>(),
										static_cast<int>(payloadBytes));
								}
							}
						}
						break;
					}
					default:
						recordReceiveError("Received packet with an unknown message type");
					}
				}
			}
			break;
		}
		case -1:
			if (!threadShouldExit()) {
				recordReceiveError("Error waiting for data on socket");
				Thread::sleep(10);
			}
			break;
		default:
			recordReceiveError("Socket returned an unexpected readiness state");
			break;
			}
		}
		catch (const std::exception& exception) {
			recordReceiveError(exception.what());
		}
		catch (...) {
			recordReceiveError("Unknown exception while receiving a packet");
		}
	}
}

void DataReceiveThread::recordReceiveError(const char* message)
{
	const auto errorNumber = receiveErrorCount_.fetch_add(1, std::memory_order_relaxed) + 1;
	if (errorNumber == 1 || errorNumber % 100 == 0) {
		std::cerr << "JammerNetz receive error " << errorNumber << ": " << message << std::endl;
	}
}

double DataReceiveThread::currentRTT() const
{
	return currentRTT_;
}

uint64_t DataReceiveThread::receiveErrorCount() const
{
	return receiveErrorCount_.load(std::memory_order_relaxed);
}

JammerNetzChannelSetup DataReceiveThread::sessionSetup() const
{
	ScopedLock lock(sessionDataLock_);
	return currentSession_;
}

std::shared_ptr<JammerNetzClientInfoMessage> DataReceiveThread::getClientInfo() const
{
	return lastClientInfoMessage_.load(std::memory_order_acquire);
}

bool DataReceiveThread::isReceivingData() const
{
	return isReceiving_;
}

void DataReceiveThread::setSessionKey(std::shared_ptr<const JammerNetzSecure::SessionKey> sessionKey) {
	ScopedLock lock(cryptoLock_);
	opener_ = sessionKey
		? std::make_unique<JammerNetzSecure::SecureDatagramOpener>(
			std::move(sessionKey), JammerNetzSecure::Direction::ServerToClient)
		: nullptr;
}
