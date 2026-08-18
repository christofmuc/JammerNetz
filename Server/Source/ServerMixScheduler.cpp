/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixScheduler.h"

#include <utility>

namespace {

ServerQueueObservation observe(const ClientQueueSnapshot& snapshot)
{
	return { snapshot.state, snapshot.size, snapshot.activityGeneration };
}

} // namespace

ServerMixScheduler::ServerMixScheduler(JammerNetzChannelSetup mixdownSetup,
	const ServerBufferConfig bufferConfig)
	: mixerCore_(std::move(mixdownSetup))
	, bufferConfig_(bufferConfig)
{
}

ServerScheduledMixResult ServerMixScheduler::process(TPacketStreamBundle& clients,
	const ClientState::TimePoint now)
{
	ServerScheduledMixResult result;
	int clientCount = 0;
	int available = 0;
	bool maximumBufferPressure = false;

	for (auto& client : clients) {
		if (!client.second) {
			continue;
		}
		if (client.second->disconnectIfGraceExpired(now)) {
			result.disconnectedClients.push_back(client.first);
		}
		const auto snapshot = client.second->snapshot();
		result.queuesBefore.emplace(client.first, observe(snapshot));
		if (snapshot.state == ClientConnectionState::Disconnected) {
			continue;
		}
		++clientCount;
		if (static_cast<int>(snapshot.size) > bufferConfig_.serverIncomingJitterBuffer) {
			++available;
		}
		if (static_cast<int>(snapshot.size) > bufferConfig_.serverIncomingMaximumBuffer) {
			maximumBufferPressure = true;
		}
	}

	const bool allClientsReady = clientCount > 0 && clientCount == available;
	result.shouldWakeAgain = maximumBufferPressure;
	if (!allClientsReady && !maximumBufferPressure && clientCount > 1) {
		result.trigger = ServerMixTrigger::None;
		result.queuesAfter = result.queuesBefore;
		return result;
	}
	if (clientCount == 1) {
		result.trigger = ServerMixTrigger::SingleClient;
	}
	else if (allClientsReady && maximumBufferPressure) {
		result.trigger = ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure;
	}
	else if (allClientsReady) {
		result.trigger = ServerMixTrigger::AllClientsReady;
	}
	else if (maximumBufferPressure) {
		result.trigger = ServerMixTrigger::MaximumBufferPressure;
	}

	std::map<std::string, std::uint64_t> observedActivity;
	for (auto& client : clients) {
		if (!client.second) {
			continue;
		}
		std::shared_ptr<JammerNetzAudioData> popped;
		bool isFillIn = false;
		std::uint64_t activityGeneration = 0;
		if (client.second->tryPop(popped, isFillIn, activityGeneration)) {
			result.incoming.emplace(client.first, std::move(popped));
			if (isFillIn) {
				result.shouldWakeAgain = true;
			}
		}
		else if (client.second->snapshot().state != ClientConnectionState::Disconnected) {
			observedActivity.emplace(client.first, activityGeneration);
		}
	}

	for (auto& client : clients) {
		if (!client.second) {
			continue;
		}
		const auto observation = observedActivity.find(client.first);
		if (observation != observedActivity.end()
			&& client.second->markUnderrun(observation->second, now)) {
			result.underrunClients.push_back(client.first);
		}
		result.queuesAfter.emplace(client.first, observe(client.second->snapshot()));
	}

	result.mix = mixerCore_.mix(result.incoming);
	return result;
}
