/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SessionControlHub.h"

#include <algorithm>
#include <utility>

namespace {
bool isServerOwnedTopic(const std::string& topic)
{
	return topic == JammerNetzControlProtocol::WelcomeTopic
		|| topic == JammerNetzControlProtocol::AcknowledgementTopic
		|| topic == JammerNetzControlProtocol::RejectionTopic
		|| topic == JammerNetzControlProtocol::PongTopic;
}
}

SessionControlHub::SessionControlHub(const uint64_t sessionEpoch, Authorizer authorizer)
	: sessionEpoch_(sessionEpoch == 0 ? 1 : sessionEpoch)
	, authorizer_(std::move(authorizer))
{
}

SessionControlResult SessionControlHub::process(const std::string& sourceEndpoint,
	const JammerNetzControlEnvelopeData& incoming)
{
	SessionControlResult result;
	if (!incoming.isStructurallyValid()) {
		++stats_.rejected;
		return result;
	}

	if (incoming.topic == JammerNetzControlProtocol::HelloTopic) {
		if (!incoming.payload.is_object() || !incoming.payload.contains("instance")
			|| !incoming.payload["instance"].is_string()
			|| incoming.payload["instance"].get_ref<const std::string&>().empty()
			|| !incoming.payload.contains("versions")
			|| !incoming.payload["versions"].is_array()
			|| std::find(incoming.payload["versions"].begin(), incoming.payload["versions"].end(),
				JammerNetzControlProtocol::Current) == incoming.payload["versions"].end()
			|| incoming.route != JammerNetzControlRoute::Server
			|| incoming.sessionEpoch != 0 || incoming.senderId != 0) {
			++stats_.rejected;
			return result;
		}
		auto& participant = registerParticipant(sourceEndpoint,
			incoming.payload["instance"].get<std::string>());
		participant.lastMessageId = incoming.messageId;
		auto welcome = makeServerEnvelope(participant,
			JammerNetzControlProtocol::WelcomeTopic, incoming.messageId,
			{{ "participant_id", participant.id }, { "session_epoch", sessionEpoch_ }});
		result.outbound.push_back({ participant.endpoint, std::move(welcome) });
		replayRetained(result, participant);
		result.accepted = true;
		++stats_.accepted;
		return result;
	}

	auto participantIt = participantsByEndpoint_.find(sourceEndpoint);
	if (participantIt == participantsByEndpoint_.end()) {
		++stats_.rejected;
		return result;
	}
	auto& participant = participantIt->second;
	if (incoming.sessionEpoch != sessionEpoch_) {
		appendRejection(result, participant, incoming.messageId, "stale_session");
		++stats_.rejected;
		return result;
	}
	if (incoming.messageId <= participant.lastMessageId) {
		if (incoming.delivery == JammerNetzControlDelivery::Acknowledged) {
			appendAcknowledgement(result, participant, incoming.messageId, "duplicate");
		}
		++stats_.duplicates;
		return result;
	}
	participant.lastMessageId = incoming.messageId;

	auto routed = incoming;
	routed.senderId = participant.id;
	routed.acknowledgementFor = 0;
	if (isServerOwnedTopic(routed.topic)) {
		appendRejection(result, participant, routed.messageId, "reserved_topic");
		++stats_.rejected;
		return result;
	}
	if (authorizer_ && !authorizer_(participant.id, routed)) {
		appendRejection(result, participant, routed.messageId, "unauthorized");
		++stats_.unauthorized;
		++stats_.rejected;
		return result;
	}
	switch (routed.route) {
	case JammerNetzControlRoute::Server:
		if (routed.topic == JammerNetzControlProtocol::PingTopic) {
			auto pong = makeServerEnvelope(participant,
				JammerNetzControlProtocol::PongTopic, routed.messageId, routed.payload);
			result.outbound.push_back({ participant.endpoint, std::move(pong) });
		}
		else {
			result.serverMessages.push_back(routed);
			if (routed.delivery == JammerNetzControlDelivery::Acknowledged) {
				appendAcknowledgement(result, participant, routed.messageId, "accepted");
			}
		}
		break;
	case JammerNetzControlRoute::Unicast: {
		const auto endpointIt = endpointByParticipantId_.find(routed.targetId);
		if (endpointIt == endpointByParticipantId_.end()) {
			appendRejection(result, participant, routed.messageId, "target_not_found");
			++stats_.rejected;
			return result;
		}
		if (routed.delivery == JammerNetzControlDelivery::Retained) {
			retained_[{ routed.topic, routed.senderId, routed.route, routed.targetId }] = routed;
		}
		result.outbound.push_back({ endpointIt->second, routed });
		++stats_.routedUnicast;
		if (routed.delivery == JammerNetzControlDelivery::Acknowledged) {
			appendAcknowledgement(result, participant, routed.messageId, "routed");
		}
		break;
	}
	case JammerNetzControlRoute::Broadcast:
		if (routed.delivery == JammerNetzControlDelivery::Retained) {
			retained_[{ routed.topic, routed.senderId, routed.route, routed.targetId }] = routed;
		}
		for (const auto& [endpoint, target] : participantsByEndpoint_) {
			if (!routed.includeSender && target.id == participant.id) {
				continue;
			}
			result.outbound.push_back({ endpoint, routed });
			++stats_.routedBroadcast;
		}
		if (routed.delivery == JammerNetzControlDelivery::Acknowledged) {
			appendAcknowledgement(result, participant, routed.messageId, "routed");
		}
		break;
	}

	result.accepted = true;
	++stats_.accepted;
	return result;
}

SessionControlResult SessionControlHub::publish(const std::string& topic,
	nlohmann::json payload, const JammerNetzControlRoute route,
	const uint32_t targetId, const JammerNetzControlDelivery delivery)
{
	SessionControlResult result;
	JammerNetzControlEnvelopeData envelope;
	envelope.sessionEpoch = sessionEpoch_;
	envelope.targetId = targetId;
	envelope.messageId = nextServerMessageId_++;
	envelope.route = route;
	envelope.delivery = delivery;
	envelope.topic = topic;
	envelope.payload = std::move(payload);
	if (!envelope.isStructurallyValid() || route == JammerNetzControlRoute::Server
		|| isServerOwnedTopic(topic)) {
		++stats_.rejected;
		return result;
	}

	if (route == JammerNetzControlRoute::Unicast) {
		const auto endpoint = endpointByParticipantId_.find(targetId);
		if (endpoint == endpointByParticipantId_.end()) {
			++stats_.rejected;
			return result;
		}
		result.outbound.push_back({ endpoint->second, envelope });
		++stats_.routedUnicast;
	}
	else {
		for (const auto& [endpoint, participant] : participantsByEndpoint_) {
			juce::ignoreUnused(participant);
			result.outbound.push_back({ endpoint, envelope });
			++stats_.routedBroadcast;
		}
	}
	if (delivery == JammerNetzControlDelivery::Retained) {
		retained_[{ envelope.topic, 0, envelope.route, envelope.targetId }] = envelope;
	}
	result.accepted = true;
	++stats_.accepted;
	return result;
}

void SessionControlHub::disconnect(const std::string& endpoint)
{
	retireParticipant(endpoint);
}

uint64_t SessionControlHub::sessionEpoch() const noexcept
{
	return sessionEpoch_;
}

std::size_t SessionControlHub::participantCount() const noexcept
{
	return participantsByEndpoint_.size();
}

SessionControlHubStats SessionControlHub::stats() const noexcept
{
	return stats_;
}

SessionControlHub::Participant& SessionControlHub::registerParticipant(
	const std::string& endpoint, const std::string& instance)
{
	auto found = participantsByEndpoint_.find(endpoint);
	if (found != participantsByEndpoint_.end() && found->second.instance == instance) {
		return found->second;
	}
	if (found != participantsByEndpoint_.end()) {
		retireParticipant(endpoint);
	}
	const auto previousEndpoint = endpointByInstance_.find(instance);
	if (previousEndpoint != endpointByInstance_.end()) {
		retireParticipant(previousEndpoint->second);
	}

	uint32_t id = nextParticipantId_++;
	if (id == 0) {
		id = nextParticipantId_++;
	}
	Participant participant { id, endpoint, instance, 0 };
	auto inserted = participantsByEndpoint_.emplace(endpoint, std::move(participant));
	endpointByParticipantId_[id] = endpoint;
	endpointByInstance_[instance] = endpoint;
	return inserted.first->second;
}

void SessionControlHub::retireParticipant(const std::string& endpoint)
{
	const auto participant = participantsByEndpoint_.find(endpoint);
	if (participant == participantsByEndpoint_.end()) {
		return;
	}
	const auto retiredId = participant->second.id;
	endpointByParticipantId_.erase(retiredId);
	endpointByInstance_.erase(participant->second.instance);
	participantsByEndpoint_.erase(participant);
	for (auto retained = retained_.begin(); retained != retained_.end();) {
		if (retained->second.senderId == retiredId
			|| (retained->second.route == JammerNetzControlRoute::Unicast
				&& retained->second.targetId == retiredId)) {
			retained = retained_.erase(retained);
		}
		else {
			++retained;
		}
	}
}

JammerNetzControlEnvelopeData SessionControlHub::makeServerEnvelope(
	const Participant& target, const char* topic, const uint64_t acknowledgementFor,
	nlohmann::json payload)
{
	JammerNetzControlEnvelopeData envelope;
	envelope.sessionEpoch = sessionEpoch_;
	envelope.targetId = target.id;
	envelope.messageId = nextServerMessageId_++;
	envelope.acknowledgementFor = acknowledgementFor;
	envelope.route = JammerNetzControlRoute::Unicast;
	envelope.topic = topic;
	envelope.payload = std::move(payload);
	return envelope;
}

void SessionControlHub::appendAcknowledgement(SessionControlResult& result,
	const Participant& target, const uint64_t acknowledgementFor, const char* status)
{
	auto acknowledgement = makeServerEnvelope(target,
		JammerNetzControlProtocol::AcknowledgementTopic, acknowledgementFor,
		{{ "status", status }});
	result.outbound.push_back({ target.endpoint, std::move(acknowledgement) });
}

void SessionControlHub::appendRejection(SessionControlResult& result,
	const Participant& target, const uint64_t acknowledgementFor, const char* reason)
{
	auto rejection = makeServerEnvelope(target,
		JammerNetzControlProtocol::RejectionTopic, acknowledgementFor,
		{{ "reason", reason }});
	result.outbound.push_back({ target.endpoint, std::move(rejection) });
}

void SessionControlHub::replayRetained(SessionControlResult& result,
	const Participant& target) const
{
	for (const auto& [key, envelope] : retained_) {
		juce::ignoreUnused(key);
		if (envelope.route == JammerNetzControlRoute::Broadcast
			|| (envelope.route == JammerNetzControlRoute::Unicast
				&& envelope.targetId == target.id)) {
			result.outbound.push_back({ target.endpoint, envelope });
		}
	}
}
