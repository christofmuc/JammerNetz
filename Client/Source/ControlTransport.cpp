/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ControlTransport.h"

#include "Client.h"

#include <algorithm>
#include <utility>

namespace {
std::string createClientInstance()
{
	const auto randomValue = static_cast<uint64_t>(Random::getSystemRandom().nextInt64());
	return String::toHexString(static_cast<int64>(randomValue)).toStdString();
}
}

ControlTransport::ControlTransport(Client& client)
	: Thread("ControlTransport")
	, client_(client)
	, clientInstance_(createClientInstance())
{
}

ControlTransport::~ControlTransport()
{
	shutdown();
}

void ControlTransport::run()
{
	while (!threadShouldExit()) {
		wakeEvent_.wait(50);
		processIncoming();
		processOutgoing();
		retryAcknowledged();
	}
}

void ControlTransport::shutdown()
{
	if (!isThreadRunning()) {
		return;
	}
	signalThreadShouldExit();
	wakeEvent_.signal();
	waitForThreadToExit(2000);
}

void ControlTransport::setSupported(const bool supported)
{
	supported_.store(supported, std::memory_order_release);
	if (supported && !helloQueued_.exchange(true, std::memory_order_acq_rel)) {
		sendHello();
	}
}

bool ControlTransport::enqueueIncoming(const JammerNetzControlEnvelopeData& envelope)
{
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		if (incoming_.size() >= QueueCapacity) {
			incomingOverflows_.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		incoming_.push_back(envelope);
	}
	wakeEvent_.signal();
	return true;
}

bool ControlTransport::send(const std::string& topic, nlohmann::json payload,
	const JammerNetzControlRoute route, const uint32_t targetId,
	const JammerNetzControlDelivery delivery, const bool includeSender,
	const uint64_t sequence)
{
	const auto epoch = sessionEpoch_.load(std::memory_order_acquire);
	if (epoch == 0 || participantId_.load(std::memory_order_acquire) == 0) {
		return false;
	}
	JammerNetzControlEnvelopeData envelope;
	envelope.sessionEpoch = epoch;
	envelope.messageId = nextMessageId_.fetch_add(1, std::memory_order_relaxed);
	envelope.sequence = sequence;
	envelope.route = route;
	envelope.targetId = targetId;
	envelope.delivery = delivery;
	envelope.includeSender = includeSender;
	envelope.topic = topic;
	envelope.payload = std::move(payload);
	if (!envelope.isStructurallyValid()) {
		return false;
	}
	return enqueueOutgoing(std::move(envelope));
}

bool ControlTransport::pollEvent(JammerNetzControlEnvelopeData& envelope)
{
	const std::lock_guard<std::mutex> lock(queueMutex_);
	if (events_.empty()) {
		return false;
	}
	envelope = std::move(events_.front());
	events_.pop_front();
	return true;
}

bool ControlTransport::isReady() const noexcept
{
	return participantId_.load(std::memory_order_acquire) != 0
		&& sessionEpoch_.load(std::memory_order_acquire) != 0;
}

uint32_t ControlTransport::participantId() const noexcept
{
	return participantId_.load(std::memory_order_acquire);
}

uint64_t ControlTransport::sessionEpoch() const noexcept
{
	return sessionEpoch_.load(std::memory_order_acquire);
}

ControlTransportStats ControlTransport::stats() const noexcept
{
	return {
		sent_.load(std::memory_order_relaxed),
		received_.load(std::memory_order_relaxed),
		retries_.load(std::memory_order_relaxed),
		outgoingOverflows_.load(std::memory_order_relaxed),
		incomingOverflows_.load(std::memory_order_relaxed),
		eventOverflows_.load(std::memory_order_relaxed),
		acknowledgementTimeouts_.load(std::memory_order_relaxed)
	};
}

bool ControlTransport::enqueueOutgoing(JammerNetzControlEnvelopeData envelope)
{
	const bool wakeImmediately = envelope.delivery != JammerNetzControlDelivery::Ephemeral;
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		if (envelope.delivery == JammerNetzControlDelivery::Ephemeral) {
			const auto existing = std::find_if(outgoing_.begin(), outgoing_.end(),
				[&envelope](const JammerNetzControlEnvelopeData& queued) {
					return queued.delivery == JammerNetzControlDelivery::Ephemeral
						&& queued.topic == envelope.topic
						&& queued.route == envelope.route
						&& queued.targetId == envelope.targetId;
				});
			if (existing != outgoing_.end()) {
				*existing = std::move(envelope);
				wakeEvent_.signal();
				return true;
			}
		}
		if (outgoing_.size() >= QueueCapacity) {
			outgoingOverflows_.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		outgoing_.push_back(std::move(envelope));
	}
	// Ephemeral controls are intentionally drained on the 50 ms timer. This caps
	// their send rate and gives latest-value coalescing a chance to take effect.
	if (wakeImmediately) {
		wakeEvent_.signal();
	}
	return true;
}

void ControlTransport::processIncoming()
{
	for (;;) {
		JammerNetzControlEnvelopeData envelope;
		{
			const std::lock_guard<std::mutex> lock(queueMutex_);
			if (incoming_.empty()) {
				return;
			}
			envelope = std::move(incoming_.front());
			incoming_.pop_front();
		}
		received_.fetch_add(1, std::memory_order_relaxed);
		if (envelope.topic == JammerNetzControlProtocol::WelcomeTopic) {
			acceptWelcome(envelope);
		}
		if (envelope.topic == JammerNetzControlProtocol::RejectionTopic
			&& envelope.payload.is_object()
			&& envelope.payload.value("reason", std::string {}) == "stale_session") {
			participantId_.store(0, std::memory_order_release);
			sessionEpoch_.store(0, std::memory_order_release);
			pending_.clear();
			helloQueued_.store(true, std::memory_order_release);
			sendHello();
		}
		if (envelope.acknowledgementFor != 0) {
			pending_.erase(envelope.acknowledgementFor);
		}
		publishEvent(std::move(envelope));
	}
}

void ControlTransport::processOutgoing()
{
	for (;;) {
		JammerNetzControlEnvelopeData envelope;
		{
			const std::lock_guard<std::mutex> lock(queueMutex_);
			if (outgoing_.empty()) {
				return;
			}
			envelope = std::move(outgoing_.front());
			outgoing_.pop_front();
		}
		if (!client_.sendControlEnvelope(envelope)) {
			continue;
		}
		sent_.fetch_add(1, std::memory_order_relaxed);
		if (envelope.delivery == JammerNetzControlDelivery::Acknowledged
			|| envelope.topic == JammerNetzControlProtocol::HelloTopic) {
			pending_[envelope.messageId] = {
				envelope, Time::getMillisecondCounter(), 1
			};
		}
	}
}

void ControlTransport::retryAcknowledged()
{
	const auto now = Time::getMillisecondCounter();
	for (auto pending = pending_.begin(); pending != pending_.end();) {
		if (now - pending->second.lastSendMillis < RetryIntervalMillis) {
			++pending;
			continue;
		}
		if (pending->second.attempts >= MaximumAttempts) {
			JammerNetzControlEnvelopeData timeout;
			timeout.protocolVersion = JammerNetzControlProtocol::Current;
			timeout.sessionEpoch = sessionEpoch();
			timeout.targetId = participantId();
			timeout.messageId = nextMessageId_.fetch_add(1, std::memory_order_relaxed);
			timeout.acknowledgementFor = pending->first;
			timeout.route = JammerNetzControlRoute::Unicast;
			timeout.topic = JammerNetzControlProtocol::RejectionTopic;
			timeout.payload = {{ "reason", "acknowledgement_timeout" }};
			publishEvent(std::move(timeout));
			acknowledgementTimeouts_.fetch_add(1, std::memory_order_relaxed);
			pending = pending_.erase(pending);
			continue;
		}
		pending->second.lastSendMillis = now;
		++pending->second.attempts;
		if (client_.sendControlEnvelope(pending->second.envelope)) {
			retries_.fetch_add(1, std::memory_order_relaxed);
		}
		++pending;
	}
}

void ControlTransport::publishEvent(JammerNetzControlEnvelopeData envelope)
{
	const std::lock_guard<std::mutex> lock(queueMutex_);
	if (events_.size() >= QueueCapacity) {
		events_.pop_front();
		eventOverflows_.fetch_add(1, std::memory_order_relaxed);
	}
	events_.push_back(std::move(envelope));
}

void ControlTransport::sendHello()
{
	JammerNetzControlEnvelopeData hello;
	hello.messageId = nextMessageId_.fetch_add(1, std::memory_order_relaxed);
	hello.delivery = JammerNetzControlDelivery::Acknowledged;
	hello.topic = JammerNetzControlProtocol::HelloTopic;
	hello.payload = {
		{ "instance", clientInstance_ },
		{ "versions", { JammerNetzControlProtocol::Current } }
	};
	enqueueOutgoing(std::move(hello));
}

void ControlTransport::acceptWelcome(const JammerNetzControlEnvelopeData& envelope)
{
	if (!envelope.payload.is_object()
		|| !envelope.payload.contains("participant_id")
		|| !envelope.payload["participant_id"].is_number_unsigned()
		|| !envelope.payload.contains("session_epoch")
		|| !envelope.payload["session_epoch"].is_number_unsigned()) {
		return;
	}
	const auto participant = envelope.payload["participant_id"].get<uint32_t>();
	const auto epoch = envelope.payload["session_epoch"].get<uint64_t>();
	if (participant == 0 || epoch == 0) {
		return;
	}
	participantId_.store(participant, std::memory_order_release);
	sessionEpoch_.store(epoch, std::memory_order_release);
}
