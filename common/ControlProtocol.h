/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace JammerNetzControlProtocol {
constexpr uint16_t Version1 = 1;
constexpr uint16_t Current = Version1;
constexpr std::size_t MaximumTopicBytes = 96;
constexpr std::size_t MaximumPayloadBytes = 1024;

constexpr const char* HelloTopic = "jn.control.hello.v1";
constexpr const char* WelcomeTopic = "jn.control.welcome.v1";
constexpr const char* AcknowledgementTopic = "jn.control.ack.v1";
constexpr const char* RejectionTopic = "jn.control.reject.v1";
constexpr const char* PingTopic = "jn.control.ping.v1";
constexpr const char* PongTopic = "jn.control.pong.v1";
}

enum class JammerNetzControlRoute : uint8_t {
	Server = 0,
	Unicast = 1,
	Broadcast = 2
};

enum class JammerNetzControlDelivery : uint8_t {
	Ephemeral = 0,
	Acknowledged = 1,
	Retained = 2
};

struct JammerNetzControlEnvelopeData {
	uint16_t protocolVersion { JammerNetzControlProtocol::Current };
	uint64_t sessionEpoch { 0 };
	uint32_t senderId { 0 };
	uint32_t targetId { 0 };
	uint64_t messageId { 0 };
	uint64_t sequence { 0 };
	uint64_t acknowledgementFor { 0 };
	JammerNetzControlRoute route { JammerNetzControlRoute::Server };
	JammerNetzControlDelivery delivery { JammerNetzControlDelivery::Ephemeral };
	bool includeSender { false };
	std::string topic;
	nlohmann::json payload { nlohmann::json::object() };

	[[nodiscard]] bool isStructurallyValid() const;
};
