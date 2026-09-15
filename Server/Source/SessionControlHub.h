/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "ControlProtocol.h"

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

struct RoutedControlEnvelope {
	std::string targetEndpoint;
	JammerNetzControlEnvelopeData envelope;
};

struct SessionControlResult {
	bool accepted { false };
	std::vector<RoutedControlEnvelope> outbound;
	std::vector<JammerNetzControlEnvelopeData> serverMessages;
};

struct SessionControlHubStats {
	uint64_t accepted { 0 };
	uint64_t rejected { 0 };
	uint64_t duplicates { 0 };
	uint64_t unauthorized { 0 };
	uint64_t routedUnicast { 0 };
	uint64_t routedBroadcast { 0 };
};

class SessionControlHub {
public:
	using Authorizer = std::function<bool(uint32_t, const JammerNetzControlEnvelopeData&)>;

	explicit SessionControlHub(uint64_t sessionEpoch, Authorizer authorizer = {});

	SessionControlResult process(const std::string& sourceEndpoint,
		const JammerNetzControlEnvelopeData& incoming);
	SessionControlResult publish(const std::string& topic, nlohmann::json payload,
		JammerNetzControlRoute route, uint32_t targetId = 0,
		JammerNetzControlDelivery delivery = JammerNetzControlDelivery::Ephemeral);
	void disconnect(const std::string& endpoint);

	[[nodiscard]] uint64_t sessionEpoch() const noexcept;
	[[nodiscard]] std::size_t participantCount() const noexcept;
	[[nodiscard]] SessionControlHubStats stats() const noexcept;

private:
	struct Participant {
		uint32_t id { 0 };
		std::string endpoint;
		std::string instance;
		uint64_t lastMessageId { 0 };
	};

	using RetainedKey = std::tuple<std::string, uint32_t, JammerNetzControlRoute, uint32_t>;

	Participant& registerParticipant(const std::string& endpoint, const std::string& instance);
	void retireParticipant(const std::string& endpoint);
	JammerNetzControlEnvelopeData makeServerEnvelope(const Participant& target,
		const char* topic, uint64_t acknowledgementFor, nlohmann::json payload);
	void appendAcknowledgement(SessionControlResult& result, const Participant& target,
		uint64_t acknowledgementFor, const char* status);
	void appendRejection(SessionControlResult& result, const Participant& target,
		uint64_t acknowledgementFor, const char* reason);
	void replayRetained(SessionControlResult& result, const Participant& target) const;

	uint64_t sessionEpoch_;
	uint32_t nextParticipantId_ { 1 };
	uint64_t nextServerMessageId_ { 1 };
	std::map<std::string, Participant> participantsByEndpoint_;
	std::map<uint32_t, std::string> endpointByParticipantId_;
	std::map<std::string, std::string> endpointByInstance_;
	std::map<RetainedKey, JammerNetzControlEnvelopeData> retained_;
	Authorizer authorizer_;
	SessionControlHubStats stats_;
};
