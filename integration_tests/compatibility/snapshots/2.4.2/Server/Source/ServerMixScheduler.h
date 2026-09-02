/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "BuffersConfig.h"
#include "ServerMixerCore.h"
#include "SharedServerTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class ServerMixTrigger {
	None,
	SingleClient,
	AllClientsReady,
	MaximumBufferPressure,
	AllClientsReadyAndMaximumBufferPressure
};

struct ServerQueueObservation {
	ClientConnectionState state { ClientConnectionState::Disconnected };
	std::size_t size { 0 };
	std::uint64_t activityGeneration { 0 };
};

struct ServerScheduledMixResult {
	ServerMixTrigger trigger { ServerMixTrigger::None };
	bool shouldWakeAgain { false };
	std::map<std::string, ServerQueueObservation> queuesBefore;
	std::map<std::string, ServerQueueObservation> queuesAfter;
	std::vector<std::string> disconnectedClients;
	std::vector<std::string> underrunClients;
	ServerInputPackets incoming;
	ServerMixStepResult mix;
};

// Synchronous server queue/readiness step. MixerThread supplies wake-ups and
// forwards the result; deterministic tests can drive this class directly.
class ServerMixScheduler {
public:
	ServerMixScheduler(JammerNetzChannelSetup mixdownSetup, ServerBufferConfig bufferConfig);

	ServerScheduledMixResult process(TPacketStreamBundle& clients,
		ClientState::TimePoint now = ClientState::Clock::now());

private:
	ServerMixerCore mixerCore_;
	ServerBufferConfig bufferConfig_;
};
