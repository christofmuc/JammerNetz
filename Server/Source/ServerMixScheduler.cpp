/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixScheduler.h"

#include <algorithm>
#include <utility>

namespace {

ServerQueueObservation observe(const ClientQueueSnapshot& snapshot)
{
	return { snapshot.state, snapshot.size, snapshot.activityGeneration };
}

bool isReady(const ServerQueueObservation& observation, const int jitterBuffer,
	const bool singleClient)
{
	if (observation.state == ClientConnectionState::Disconnected) {
		return false;
	}
	return singleClient ? observation.size > 0
		: static_cast<int>(observation.size) > jitterBuffer;
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
	std::map<std::string, ServerQueueObservation> queuesAfterFastForward;
	ServerMixRecipients recipients;
	const auto maximumQueueDepth = static_cast<std::size_t>(
		std::max(0, bufferConfig_.serverIncomingMaximumBuffer));
	const auto targetQueueDepth = std::min(maximumQueueDepth,
		static_cast<std::size_t>(std::max(0, bufferConfig_.serverIncomingJitterBuffer)));

	for (auto& client : clients) {
		if (!client.second) {
			continue;
		}
		if (client.second->disconnectIfGraceExpired(now)) {
			result.disconnectedClients.push_back(client.first);
		}
		auto pressure = client.second->applyQueuePressure(maximumQueueDepth, targetQueueDepth);
		result.queuesBefore.emplace(client.first, observe(pressure.before));
		auto snapshot = pressure.after;
		if (snapshot.state == ClientConnectionState::Disconnected) {
			queuesAfterFastForward.emplace(client.first, observe(snapshot));
			sourceHealth_.erase(client.first);
			continue;
		}
		if (pressure.fastForward.discardedPackets > 0) {
			result.fastForwardedClients.emplace(client.first, std::move(pressure.fastForward));
		}
		queuesAfterFastForward.emplace(client.first, observe(snapshot));
		if (const auto metadata = client.second->mixMetadata()) {
			recipients.emplace(client.first, *metadata);
		}
		sourceHealth_.try_emplace(client.first, 0);
	}

	const bool singleClient = recipients.size() == 1;
	auto candidate = queuesAfterFastForward.end();
	for (auto current = queuesAfterFastForward.begin(); current != queuesAfterFastForward.end(); ++current) {
		const bool rebasedByPressure = result.fastForwardedClients.count(current->first) != 0
			&& current->second.size > 0;
		if (!isReady(current->second, bufferConfig_.serverIncomingJitterBuffer, singleClient)
			&& !rebasedByPressure) {
			continue;
		}
		if (candidate == queuesAfterFastForward.end()
			|| sourceHealth_[current->first] > sourceHealth_[candidate->first]
			|| (sourceHealth_[current->first] == sourceHealth_[candidate->first]
				&& current->second.size > candidate->second.size)) {
			candidate = current;
		}
	}

	const auto previousCadenceClient = cadenceClient_;
	const auto currentCadence = queuesAfterFastForward.find(cadenceClient_);
	const bool currentCadenceActive = currentCadence != queuesAfterFastForward.end()
		&& currentCadence->second.state != ClientConnectionState::Disconnected;
	const bool currentCadenceReady = currentCadence != queuesAfterFastForward.end()
		&& (isReady(currentCadence->second, bufferConfig_.serverIncomingJitterBuffer, singleClient)
			|| (result.fastForwardedClients.count(cadenceClient_) != 0
				&& currentCadence->second.size > 0));
	if (!currentCadenceReady) {
		const bool candidateIsAtLeastAsHealthy = candidate != queuesAfterFastForward.end()
			&& (!currentCadenceActive
				|| sourceHealth_[candidate->first] >= sourceHealth_[cadenceClient_]);
		if (candidateIsAtLeastAsHealthy) {
			cadenceClient_ = candidate->first;
		}
	}
	result.cadenceClient = cadenceClient_;
	result.cadenceClientChanged = cadenceClient_ != previousCadenceClient;

	const auto selectedCadence = queuesAfterFastForward.find(cadenceClient_);
	const bool selectedCadenceReady = selectedCadence != queuesAfterFastForward.end()
		&& (isReady(selectedCadence->second, bufferConfig_.serverIncomingJitterBuffer, singleClient)
			|| (result.fastForwardedClients.count(cadenceClient_) != 0
				&& selectedCadence->second.size > 0));
	if (!selectedCadenceReady) {
		result.trigger = ServerMixTrigger::None;
		result.queuesAfter = std::move(queuesAfterFastForward);
		return result;
	}

	const bool allClientsReady = !recipients.empty()
		&& std::all_of(recipients.begin(), recipients.end(), [&](const auto& recipient) {
			const auto observation = queuesAfterFastForward.find(recipient.first);
			return observation != queuesAfterFastForward.end()
				&& isReady(observation->second, bufferConfig_.serverIncomingJitterBuffer, singleClient);
		});
	const bool hadPressure = !result.fastForwardedClients.empty();
	if (singleClient) {
		result.trigger = ServerMixTrigger::SingleClient;
	}
	else if (hadPressure && allClientsReady) {
		result.trigger = ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure;
	}
	else if (hadPressure) {
		result.trigger = ServerMixTrigger::MaximumBufferPressure;
	}
	else if (previousCadenceClient.empty() && allClientsReady) {
		result.trigger = ServerMixTrigger::AllClientsReady;
	}
	else if (result.cadenceClientChanged) {
		result.trigger = ServerMixTrigger::CadenceClientFailover;
	}
	else {
		result.trigger = ServerMixTrigger::CadenceClient;
	}

	std::map<std::string, std::uint64_t> observedActivity;
	for (const auto& recipient : recipients) {
		auto client = clients.find(recipient.first);
		if (client == clients.end()) {
			continue;
		}
		if (!client->second) {
			continue;
		}
		std::shared_ptr<JammerNetzAudioData> popped;
		bool isFillIn = false;
		std::uint64_t activityGeneration = 0;
		if (client->second->tryPop(popped, isFillIn, activityGeneration)) {
			result.incoming.emplace(client->first, std::move(popped));
			if (isFillIn) {
				result.fillInClients.push_back(client->first);
				result.contributions.emplace(client->first, ServerSourceContribution::Concealment);
				sourceHealth_[client->first] = sourceHealth_[client->first] > 0
					? sourceHealth_[client->first] - 1 : 0;
			}
			else {
				result.contributions.emplace(client->first, ServerSourceContribution::Packet);
				++sourceHealth_[client->first];
			}
		}
		else if (client->second->snapshot().state != ClientConnectionState::Disconnected) {
			observedActivity.emplace(client->first, activityGeneration);
			result.missingClients.push_back(client->first);
			result.contributions.emplace(client->first, ServerSourceContribution::Silence);
			sourceHealth_[client->first] = 0;
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

	result.mix = mixerCore_.mix(result.incoming, recipients, cadenceClient_);
	return result;
}
