/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "CharacterizationTestSupport.h"
#include "ClientState.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::shared_ptr<JammerNetzAudioData> makeReconnectPacket(const std::uint64_t counter)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	audio->clear();
	JammerNetzChannelSetup setup(true);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Mono));
	return std::make_shared<JammerNetzAudioData>(counter, static_cast<double>(counter), setup,
		SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr);
}

const char* transitionName(const ClientConnectionTransition transition)
{
	switch (transition) {
	case ClientConnectionTransition::None: return "none";
	case ClientConnectionTransition::InitialConnection: return "initial_connection";
	case ClientConnectionTransition::GraceRecovery: return "grace_recovery";
	case ClientConnectionTransition::Reconnection: return "reconnection";
	}
	return "unknown";
}

struct PrimedClient {
	std::shared_ptr<ClientState> client;
	std::uint64_t observedGeneration { 0 };
};

PrimedClient primeClient()
{
	PrimedClient result { std::make_shared<ClientState>("same-endpoint") };
	result.client->push(makeReconnectPacket(100), 0, ClientState::TimePoint{});
	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	if (!result.client->tryPop(packet, isFillIn, result.observedGeneration)) {
		throw std::runtime_error("Could not prime reconnect characterization");
	}
	return result;
}

nlohmann::json characterizeReconnectMatrix()
{
	using namespace std::chrono_literals;
	const auto start = ClientState::TimePoint{};
	nlohmann::json rows = nlohmann::json::array();

	{
		auto primed = primeClient();
		const auto push = primed.client->push(makeReconnectPacket(101), 0, start + 1ms);
		const bool marked = primed.client->markUnderrun(primed.observedGeneration, start + 2ms);
		const auto snapshot = primed.client->snapshot();
		rows.push_back({
			{ "ordering", "packet_before_stale_underrun_decision" },
			{ "packet_queued", push.queued },
			{ "transition", transitionName(push.transition) },
			{ "underrun_marked", marked },
			{ "final_state", jammernetz::test::connectionStateName(snapshot.state) },
			{ "queue_size", snapshot.size }
		});
	}

	{
		auto primed = primeClient();
		const bool marked = primed.client->markUnderrun(primed.observedGeneration, start);
		const auto push = primed.client->push(makeReconnectPacket(101), 0, start + 1s);
		const auto snapshot = primed.client->snapshot();
		rows.push_back({
			{ "ordering", "packet_after_underrun_before_expiry" },
			{ "packet_queued", push.queued },
			{ "transition", transitionName(push.transition) },
			{ "underrun_marked", marked },
			{ "final_state", jammernetz::test::connectionStateName(snapshot.state) },
			{ "queue_size", snapshot.size }
		});
	}

	{
		auto primed = primeClient();
		primed.client->markUnderrun(primed.observedGeneration, start);
		const bool expired = primed.client->disconnectIfGraceExpired(start + 2s);
		const auto push = primed.client->push(makeReconnectPacket(1), 0, start + 2s);
		const auto snapshot = primed.client->snapshot();
		rows.push_back({
			{ "ordering", "expiry_before_reset_counter_packet" },
			{ "grace_expired", expired },
			{ "packet_queued", push.queued },
			{ "transition", transitionName(push.transition) },
			{ "final_state", jammernetz::test::connectionStateName(snapshot.state) },
			{ "queue_size", snapshot.size }
		});
	}

	{
		auto primed = primeClient();
		primed.client->markUnderrun(primed.observedGeneration, start);
		const auto push = primed.client->push(makeReconnectPacket(1), 0, start + 2s);
		const bool expired = primed.client->disconnectIfGraceExpired(start + 2s);
		const auto snapshot = primed.client->snapshot();
		rows.push_back({
			{ "ordering", "reset_counter_packet_before_expiry_at_deadline" },
			{ "grace_expired", expired },
			{ "packet_queued", push.queued },
			{ "transition", transitionName(push.transition) },
			{ "final_state", jammernetz::test::connectionStateName(snapshot.state) },
			{ "queue_size", snapshot.size },
			{ "stalled_reset_counter", !push.queued && snapshot.state == ClientConnectionState::Connected }
		});
	}

	{
		auto primed = primeClient();
		primed.client->markUnderrun(primed.observedGeneration, start);
		primed.client->disconnectIfGraceExpired(start + 2s);
		const auto reconnect = primed.client->push(makeReconnectPacket(1), 0, start + 2s);
		const auto delayedOld = primed.client->push(makeReconnectPacket(101), 0, start + 2s + 1ms);
		JammerNetzStreamQualityInfo quality;
		primed.client->qualityInfo(quality);
		const auto snapshot = primed.client->snapshot();
		rows.push_back({
			{ "ordering", "delayed_old_packet_after_completed_reconnect" },
			{ "reconnect_queued", reconnect.queued },
			{ "old_packet_queued", delayedOld.queued },
			{ "final_state", jammernetz::test::connectionStateName(snapshot.state) },
			{ "queue_size", snapshot.size },
			{ "old_generation_contamination", delayedOld.queued },
			{ "out_of_order_packets", quality.outOfOrderPacketCounter }
		});
	}

	return {
		{ "scenario", "disconnect_reconnect_event_order" },
		{ "grace_period_milliseconds",
			std::chrono::duration_cast<std::chrono::milliseconds>(ClientState::DisconnectGracePeriod).count() },
		{ "results", rows }
	};
}

TEST(ReconnectCharacterizationTest, EventOrderMatrixIsDeterministicAndWritesObservedOutcomes)
{
	const auto first = characterizeReconnectMatrix();
	const auto replay = characterizeReconnectMatrix();
	EXPECT_EQ(first, replay);
	ASSERT_EQ(first.at("results").size(), 5U);

	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto path = artifactDirectory.getChildFile("disconnect-reconnect").getChildFile("summary.json");
	jammernetz::test::writeJsonArtifact(path, first, "reconnect characterization");
	RecordProperty("characterization_summary", first.dump());
}

} // namespace
