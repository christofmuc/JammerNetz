/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "BuffersConfig.h"
#include "CharacterizationTestSupport.h"
#include "DeterministicAudioTestSupport.h"
#include "JammerNetzAudioEngine.h"
#include "ServerMixScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using jammernetz::test::SampleIndex;
using jammernetz::test::ScenarioTrace;
using jammernetz::test::SignalOracle;
using jammernetz::test::SyntheticAudioSource;

constexpr std::size_t warmupFrames = 16;
constexpr std::size_t recoveryFrames = 64;
constexpr std::size_t coherentRecoveryWindow = 8;
constexpr float comparisonEpsilon = 1.0e-5f;

const char* triggerName(const ServerMixTrigger trigger)
{
	switch (trigger) {
	case ServerMixTrigger::None: return "none";
	case ServerMixTrigger::SingleClient: return "single_client";
	case ServerMixTrigger::AllClientsReady: return "all_clients_ready";
	case ServerMixTrigger::MaximumBufferPressure: return "maximum_buffer_pressure";
	case ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure:
		return "all_clients_ready_and_maximum_buffer_pressure";
	}
	return "unknown";
}

JammerNetzChannelSetup monoSetup(const JammerNetzChannelTarget target)
{
	JammerNetzChannelSetup setup(true);
	setup.channels.push_back(JammerNetzSingleChannelSetup(static_cast<uint8>(target)));
	return setup;
}

JammerNetzChannelSetup stereoMixdown()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

std::shared_ptr<JammerNetzAudioData> makePacket(const std::uint32_t sourceId,
	const std::uint64_t frameIndex,
	const JammerNetzChannelSetup& setup)
{
	auto audio = std::make_shared<AudioBuffer<float>>(1, SAMPLE_BUFFER_SIZE);
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		audio->setSample(0, sample, SyntheticAudioSource::valueAt(sourceId, 0,
			frameIndex * SAMPLE_BUFFER_SIZE + static_cast<std::uint64_t>(sample)));
	}
	const auto timestamp = 1000.0 * static_cast<double>(frameIndex * SAMPLE_BUFFER_SIZE)
		/ static_cast<double>(SAMPLE_RATE);
	return std::make_shared<JammerNetzAudioData>(100 + frameIndex, timestamp, setup,
		SAMPLE_RATE, 0.0f, MidiSignal_None, std::move(audio), nullptr);
}

AudioBuffer<float> idealReceiverFrame(const std::string& receiver,
	const std::uint64_t frameIndex)
{
	AudioBuffer<float> result(2, SAMPLE_BUFFER_SIZE);
	result.clear();
	const bool receiverIsA = receiver == "client-a";
	const std::uint32_t remoteSource = receiverIsA ? 2U : 1U;
	const int outputChannel = receiverIsA ? 1 : 0;
	for (int sample = 0; sample < SAMPLE_BUFFER_SIZE; ++sample) {
		result.setSample(outputChannel, sample, SyntheticAudioSource::valueAt(remoteSource, 0,
			frameIndex * SAMPLE_BUFFER_SIZE + static_cast<std::uint64_t>(sample)));
	}
	return result;
}

std::size_t differingSamples(const std::vector<jammernetz::test::DiscrepancySpan>& spans)
{
	std::size_t result = 0;
	for (const auto& span : spans) {
		result += static_cast<std::size_t>(span.lastSample - span.firstSample + 1U);
	}
	return result;
}

float maximumError(const std::vector<jammernetz::test::DiscrepancySpan>& spans)
{
	float result = 0.0f;
	for (const auto& span : spans) {
		result = std::max(result, span.maximumAbsoluteError);
	}
	return result;
}

double percentage(const std::size_t numerator, const std::size_t denominator)
{
	return denominator == 0 ? 0.0
		: 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

double framesToMilliseconds(const std::size_t frames)
{
	return 1000.0 * static_cast<double>(frames * SAMPLE_BUFFER_SIZE)
		/ static_cast<double>(SAMPLE_RATE);
}

struct ReceiverQualityResult {
	bool playbackStarted { false };
	std::size_t outputFrames { 0 };
	std::size_t comparedFrames { 0 };
	std::size_t glitchFrames { 0 };
	std::size_t discrepancySpans { 0 };
	std::size_t mismatchedChannelSamples { 0 };
	float maximumAbsoluteError { 0.0f };
	std::optional<std::size_t> firstGlitchFrame;
	std::optional<std::size_t> lastGlitchFrame;
	std::size_t longestGlitchRunFrames { 0 };
	std::size_t unmatchedOutputFrames { 0 };
	std::size_t outputDiscontinuities { 0 };
	std::uint64_t maximumPlayoutSkewFrames { 0 };
	std::size_t silentOutputFramesAfterStart { 0 };
	std::size_t longestSilentRunFrames { 0 };
	std::optional<std::size_t> recoveryFrames;
	std::uint64_t playoutUnderruns { 0 };
	std::uint64_t discardedFrames { 0 };
	std::uint64_t receiveQueueOverruns { 0 };
	std::uint64_t maximumPreparedQueueFrames { 0 };

	[[nodiscard]] bool sampleExact() const noexcept
	{
		return playbackStarted && glitchFrames == 0 && playoutUnderruns == 0;
	}

	[[nodiscard]] bool recovered() const noexcept
	{
		return playbackStarted && recoveryFrames.has_value();
	}

	bool operator==(const ReceiverQualityResult&) const = default;
};

nlohmann::json receiverQualityJson(const ReceiverQualityResult& result)
{
	const auto comparedChannelSamples = result.comparedFrames * 2U
		* static_cast<std::size_t>(SAMPLE_BUFFER_SIZE);
	return {
		{ "playback_started", result.playbackStarted },
		{ "sample_exact", result.sampleExact() },
		{ "recovered", result.recovered() },
		{ "output_frames", result.outputFrames },
		{ "compared_frames", result.comparedFrames },
		{ "glitch_frames", result.glitchFrames },
		{ "glitch_frame_percent", percentage(result.glitchFrames, result.comparedFrames) },
		{ "discrepancy_spans", result.discrepancySpans },
		{ "mismatched_channel_samples", result.mismatchedChannelSamples },
		{ "mismatched_channel_sample_percent", percentage(
			result.mismatchedChannelSamples, comparedChannelSamples) },
		{ "maximum_absolute_error", result.maximumAbsoluteError },
		{ "first_glitch_frame", result.firstGlitchFrame
			? nlohmann::json(*result.firstGlitchFrame) : nlohmann::json(nullptr) },
		{ "last_glitch_frame", result.lastGlitchFrame
			? nlohmann::json(*result.lastGlitchFrame) : nlohmann::json(nullptr) },
		{ "longest_glitch_run_frames", result.longestGlitchRunFrames },
		{ "unmatched_output_frames", result.unmatchedOutputFrames },
		{ "output_discontinuities", result.outputDiscontinuities },
		{ "maximum_playout_skew_frames", result.maximumPlayoutSkewFrames },
		{ "silent_output_frames_after_start", result.silentOutputFramesAfterStart },
		{ "longest_silent_run_frames", result.longestSilentRunFrames },
		{ "recovery_frames", result.recoveryFrames
			? nlohmann::json(*result.recoveryFrames) : nlohmann::json(nullptr) },
		{ "playout_underruns", result.playoutUnderruns },
		{ "discarded_frames", result.discardedFrames },
		{ "receive_queue_overruns", result.receiveQueueOverruns },
		{ "maximum_prepared_queue_frames", result.maximumPreparedQueueFrames }
	};
}

class ReceiverQualityProbe {
public:
	ReceiverQualityProbe(std::string receiver,
		const SampleIndex recoveryEligibleSample,
		const std::uint64_t maximumSearchFrame)
		: receiver_(std::move(receiver))
		, engine_(session_, juce::File())
		, recoveryEligibleSample_(recoveryEligibleSample)
		, maximumSearchFrame_(maximumSearchFrame)
	{
		engine_.setLocalMonitoring(false);
		engine_.setMasterVolume(1.0);
		engine_.setMonitorBalance(1.0);
		engine_.setPlayoutBufferRange(CLIENT_PLAYOUT_JITTER_BUFFER, CLIENT_PLAYOUT_MAX_BUFFER);
		engine_.prepare(SAMPLE_RATE, SAMPLE_BUFFER_SIZE);
	}

	void deliver(const OutgoingPackage& package)
	{
		if (!firstSignalMessageCounter_
			&& package.audioBlock.audioBuffer->getMagnitude(0, SAMPLE_BUFFER_SIZE) > comparisonEpsilon) {
			firstSignalMessageCounter_ = package.audioBlock.messageCounter;
		}
		engine_.enqueueRemoteAudio(std::make_shared<JammerNetzAudioData>(package.audioBlock, nullptr));
		while (engine_.processNextIncomingPacket()) {}
	}

	void processCallback(const SampleIndex virtualSample)
	{
		AudioBuffer<float> observed(2, SAMPLE_BUFFER_SIZE);
		observed.clear();
		float* outputs[] { observed.getWritePointer(0), observed.getWritePointer(1) };
		engine_.process(nullptr, 0, outputs, 2, SAMPLE_BUFFER_SIZE);
		++result_.outputFrames;

		const auto playout = engine_.getPlayoutQualityInfo();
		result_.maximumPreparedQueueFrames = std::max(result_.maximumPreparedQueueFrames,
			playout.currentPlayQueueLength_);

		const bool silent = observed.getMagnitude(0, SAMPLE_BUFFER_SIZE) <= comparisonEpsilon;
		if (!result_.playbackStarted && !silent && firstSignalMessageCounter_) {
			result_.playbackStarted = true;
			continuousFrame_ = *firstSignalMessageCounter_ - 100U;
		}

		AudioBuffer<float> expected(2, SAMPLE_BUFFER_SIZE);
		expected.clear();
		bool exact = true;
		if (result_.playbackStarted) {
			expected = idealReceiverFrame(receiver_, continuousFrame_++);
			++result_.comparedFrames;
			const auto frameDifferences = compare(expected, observed);
			exact = frameDifferences.empty();
			if (!exact) {
				++result_.glitchFrames;
				if (!result_.firstGlitchFrame) {
					result_.firstGlitchFrame = result_.comparedFrames - 1U;
				}
				result_.lastGlitchFrame = result_.comparedFrames - 1U;
				currentGlitchRunFrames_++;
				result_.longestGlitchRunFrames = std::max(result_.longestGlitchRunFrames,
					currentGlitchRunFrames_);
			}
			else {
				currentGlitchRunFrames_ = 0;
			}
			if (silent) {
				++result_.silentOutputFramesAfterStart;
				currentSilentRunFrames_++;
				result_.longestSilentRunFrames = std::max(result_.longestSilentRunFrames,
					currentSilentRunFrames_);
			}
			else {
				currentSilentRunFrames_ = 0;
			}

			const auto matchedFrame = findMatchingIdealFrame(observed);
			if (!matchedFrame) {
				++result_.unmatchedOutputFrames;
				matchedRecoveryRun_ = 0;
				lastMatchedFrame_.reset();
			}
			else {
				const auto expectedFrame = continuousFrame_ - 1U;
				const auto skew = *matchedFrame > expectedFrame
					? *matchedFrame - expectedFrame : expectedFrame - *matchedFrame;
				result_.maximumPlayoutSkewFrames = std::max(result_.maximumPlayoutSkewFrames, skew);
				const bool continuous = !lastMatchedFrame_ || *matchedFrame == *lastMatchedFrame_ + 1U;
				if (lastMatchedFrame_ && !continuous) {
					++result_.outputDiscontinuities;
				}
				if (virtualSample >= recoveryEligibleSample_) {
					if (!recoveryObservationFrame_) {
						recoveryObservationFrame_ = result_.outputFrames;
					}
					matchedRecoveryRun_ = continuous ? matchedRecoveryRun_ + 1U : 1U;
					if (!result_.recoveryFrames && matchedRecoveryRun_ >= coherentRecoveryWindow) {
						result_.recoveryFrames = result_.outputFrames - *recoveryObservationFrame_ + 1U;
					}
				}
				lastMatchedFrame_ = matchedFrame;
			}
		}

		expectedAudio_.append(expected);
		observedAudio_.append(observed);
	}

	ReceiverQualityResult finish()
	{
		const auto discrepancies = SignalOracle::compare(expectedAudio_, observedAudio_, comparisonEpsilon);
		result_.discrepancySpans = discrepancies.size();
		result_.mismatchedChannelSamples = differingSamples(discrepancies);
		result_.maximumAbsoluteError = maximumError(discrepancies);
		const auto playout = engine_.getPlayoutQualityInfo();
		const auto workers = engine_.getRealtimeWorkerStats();
		result_.playoutUnderruns = playout.playUnderruns_;
		result_.discardedFrames = playout.discardedPackageCounter_;
		result_.receiveQueueOverruns = workers.receiveQueueOverruns;
		return result_;
	}

private:
	static std::vector<jammernetz::test::DiscrepancySpan> compare(
		const AudioBuffer<float>& expected,
		const AudioBuffer<float>& observed)
	{
		jammernetz::test::CapturedAudio expectedCapture;
		jammernetz::test::CapturedAudio observedCapture;
		expectedCapture.append(expected);
		observedCapture.append(observed);
		return SignalOracle::compare(expectedCapture, observedCapture, comparisonEpsilon);
	}

	std::optional<std::uint64_t> findMatchingIdealFrame(const AudioBuffer<float>& observed) const
	{
		const int signalChannel = receiver_ == "client-a" ? 1 : 0;
		const float firstSample = observed.getSample(signalChannel, 0);
		for (std::uint64_t frame = 0; frame <= maximumSearchFrame_; ++frame) {
			const auto source = receiver_ == "client-a" ? 2U : 1U;
			if (SyntheticAudioSource::valueAt(source, 0, frame * SAMPLE_BUFFER_SIZE) != firstSample) {
				continue;
			}
			const auto candidate = idealReceiverFrame(receiver_, frame);
			if (compare(candidate, observed).empty()) {
				return frame;
			}
		}
		return std::nullopt;
	}

	std::string receiver_;
	JammerNetzSession session_;
	JammerNetzAudioEngine engine_;
	SampleIndex recoveryEligibleSample_ { 0 };
	std::uint64_t maximumSearchFrame_ { 0 };
	std::optional<std::uint64_t> firstSignalMessageCounter_;
	std::uint64_t continuousFrame_ { 0 };
	std::optional<std::uint64_t> lastMatchedFrame_;
	std::optional<std::size_t> recoveryObservationFrame_;
	std::size_t matchedRecoveryRun_ { 0 };
	std::size_t currentGlitchRunFrames_ { 0 };
	std::size_t currentSilentRunFrames_ { 0 };
	jammernetz::test::CapturedAudio expectedAudio_;
	jammernetz::test::CapturedAudio observedAudio_;
	ReceiverQualityResult result_;
};

nlohmann::json queueJson(const std::map<std::string, ServerQueueObservation>& queues)
{
	nlohmann::json result = nlohmann::json::object();
	for (const auto& [name, queue] : queues) {
		result[name] = {
			{ "state", jammernetz::test::connectionStateName(queue.state) },
			{ "size", queue.size },
			{ "activity_generation", queue.activityGeneration }
		};
	}
	return result;
}

struct HoldFlushResult {
	std::size_t holdFrames { 0 };
	bool flushHeldBeforeCurrent { false };
	std::size_t mixCount { 0 };
	std::size_t maximumPressureMixes { 0 };
	std::size_t singleSourceMixes { 0 };
	std::size_t skewedMixes { 0 };
	std::uint64_t maximumSourceSkew { 0 };
	std::size_t maximumQueueA { 0 };
	std::size_t maximumQueueB { 0 };
	std::optional<std::size_t> recoveryMixesAfterFlush;
	std::size_t underrunTransitions { 0 };

	bool operator==(const HoldFlushResult&) const = default;
};

nlohmann::json toJson(const HoldFlushResult& result)
{
	return {
		{ "hold_frames", result.holdFrames },
		{ "flush_order", result.flushHeldBeforeCurrent ? "held_before_current" : "current_before_held" },
		{ "mix_count", result.mixCount },
		{ "maximum_pressure_mixes", result.maximumPressureMixes },
		{ "single_source_mixes", result.singleSourceMixes },
		{ "skewed_mixes", result.skewedMixes },
		{ "maximum_source_skew_frames", result.maximumSourceSkew },
		{ "maximum_queue_a", result.maximumQueueA },
		{ "maximum_queue_b", result.maximumQueueB },
		{ "recovery_mixes_after_flush", result.recoveryMixesAfterFlush
			? nlohmann::json(*result.recoveryMixesAfterFlush) : nlohmann::json(nullptr) },
		{ "underrun_transitions", result.underrunTransitions }
	};
}

class HoldFlushScenario {
public:
	HoldFlushScenario(const std::size_t holdFrames, const bool flushHeldBeforeCurrent)
		: clientA_(std::make_shared<ClientState>("client-a"))
		, clientB_(std::make_shared<ClientState>("client-b"))
		, scheduler_(stereoMixdown(), {
			SERVER_INCOMING_JITTER_BUFFER,
			SERVER_INCOMING_MAXIMUM_BUFFER,
			BUFFER_PREFILL_ON_CONNECT
		})
		, setupA_(monoSetup(JammerNetzChannelTarget::Left))
		, setupB_(monoSetup(JammerNetzChannelTarget::Right))
	{
		result_.holdFrames = holdFrames;
		result_.flushHeldBeforeCurrent = flushHeldBeforeCurrent;
		clients_.emplace("client-a", clientA_);
		clients_.emplace("client-b", clientB_);
	}

	HoldFlushResult run()
	{
		deliver(0, 0, "paced", false);
		deliver(1, 0, "paced", false);
		processWakeups(2);
		for (std::uint64_t frame = 1; frame <= warmupFrames; ++frame) {
			deliver(0, frame, "paced");
			deliver(1, frame, "paced");
		}

		std::vector<std::shared_ptr<JammerNetzAudioData>> held;
		const auto firstHeldFrame = static_cast<std::uint64_t>(warmupFrames + 1);
		for (std::uint64_t offset = 0; offset < result_.holdFrames; ++offset) {
			const auto frame = firstHeldFrame + offset;
			deliver(0, frame, "paced");
			held.push_back(makePacket(2, frame, setupB_));
			recordDelivery("client-b", frame, "held");
		}

		const auto currentFrame = firstHeldFrame + result_.holdFrames;
		if (!result_.flushHeldBeforeCurrent) {
			deliver(0, currentFrame, "paced");
		}
		for (const auto& packet : held) {
			pushPacket(1, packet, "flushed");
		}
		if (result_.flushHeldBeforeCurrent) {
			deliver(0, currentFrame, "paced");
		}
		deliver(1, currentFrame, "paced");
		flushComplete_ = true;
		mixCountAtFlush_ = result_.mixCount;

		for (std::uint64_t offset = 1; offset <= recoveryFrames; ++offset) {
			const auto frame = currentFrame + offset;
			deliver(0, frame, "paced");
			deliver(1, frame, "paced");
		}
		return result_;
	}

	const ScenarioTrace& trace() const noexcept { return trace_; }

private:
	ClientState::TimePoint now() const
	{
		const auto micros = static_cast<std::int64_t>(virtualSample_ * 1000000ULL / SAMPLE_RATE);
		return ClientState::TimePoint{} + std::chrono::microseconds(micros);
	}

	void recordDelivery(const std::string& client, const std::uint64_t frame, const std::string& action)
	{
		trace_.record({ virtualSample_, traceSequence_++, "delivery", {
			{ "client", client }, { "frame", frame }, { "action", action }
		} });
	}

	void deliver(const std::size_t clientIndex, const std::uint64_t frame,
		const std::string& action, const bool process = true)
	{
		const auto& setup = clientIndex == 0 ? setupA_ : setupB_;
		pushPacket(clientIndex, makePacket(static_cast<std::uint32_t>(clientIndex + 1), frame, setup),
			action, process);
		virtualSample_ += SAMPLE_BUFFER_SIZE / 2U;
	}

	void pushPacket(const std::size_t clientIndex,
		const std::shared_ptr<JammerNetzAudioData>& packet,
		const std::string& action,
		const bool process = true)
	{
		auto& client = clientIndex == 0 ? clientA_ : clientB_;
		const auto push = client->push(packet, BUFFER_PREFILL_ON_CONNECT, now());
		recordDelivery(clientIndex == 0 ? "client-a" : "client-b",
			packet->messageCounter() - 100U, action);
		if (!push.queued) {
			throw std::runtime_error("The hold-and-flush transport rejected a generated packet");
		}
		if (process) {
			processWakeups(1);
		}
	}

	void processWakeups(std::size_t count)
	{
		constexpr std::size_t maximumSteps = 4096;
		std::size_t steps = 0;
		while (count > 0) {
			--count;
			if (++steps > maximumSteps) {
				throw std::runtime_error("The scheduler kept requesting wake-ups without settling");
			}
			const auto step = scheduler_.process(clients_, now());
			recordStep(step);
			if (step.shouldWakeAgain) {
				++count;
			}
		}
	}

	void recordStep(const ServerScheduledMixResult& step)
	{
		if (step.mix.outgoing.size() != step.incoming.size()) {
			throw std::runtime_error("The server mixer did not produce one routed packet per input");
		}
		if (!step.mix.diagnostics.empty()) {
			throw std::runtime_error("The characterization input produced an invalid mixer diagnostic");
		}

		for (const auto& [name, queue] : step.queuesBefore) {
			if (name == "client-a") {
				result_.maximumQueueA = std::max(result_.maximumQueueA, queue.size);
			}
			else if (name == "client-b") {
				result_.maximumQueueB = std::max(result_.maximumQueueB, queue.size);
			}
		}
		result_.underrunTransitions += step.underrunClients.size();
		if (step.trigger == ServerMixTrigger::MaximumBufferPressure
			|| step.trigger == ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure) {
			++result_.maximumPressureMixes;
		}

		nlohmann::json selected = nlohmann::json::object();
		for (const auto& [name, packet] : step.incoming) {
			selected[name] = packet->messageCounter();
		}
		trace_.record({ virtualSample_, traceSequence_++, "server_mix_decision", {
			{ "trigger", triggerName(step.trigger) },
			{ "queues_before", queueJson(step.queuesBefore) },
			{ "queues_after", queueJson(step.queuesAfter) },
			{ "selected_counters", selected },
			{ "underrun_clients", step.underrunClients }
		} });

		if (step.incoming.empty()) {
			return;
		}
		++result_.mixCount;
		bool coherent = false;
		if (step.incoming.size() == 1U) {
			++result_.singleSourceMixes;
		}
		else if (step.incoming.size() == 2U) {
			const auto a = static_cast<std::uint64_t>(
				step.incoming.at("client-a")->messageCounter());
			const auto b = static_cast<std::uint64_t>(
				step.incoming.at("client-b")->messageCounter());
			const auto skew = a > b ? a - b : b - a;
			result_.maximumSourceSkew = std::max(result_.maximumSourceSkew, skew);
			coherent = skew == 0;
			if (!coherent) {
				++result_.skewedMixes;
			}
		}

		if (!flushComplete_ || result_.recoveryMixesAfterFlush) {
			return;
		}
		coherentRun_ = coherent ? coherentRun_ + 1U : 0U;
		if (coherentRun_ >= coherentRecoveryWindow) {
			result_.recoveryMixesAfterFlush = result_.mixCount - mixCountAtFlush_;
		}
	}

	HoldFlushResult result_;
	TPacketStreamBundle clients_;
	std::shared_ptr<ClientState> clientA_;
	std::shared_ptr<ClientState> clientB_;
	ServerMixScheduler scheduler_;
	JammerNetzChannelSetup setupA_;
	JammerNetzChannelSetup setupB_;
	ScenarioTrace trace_;
	SampleIndex virtualSample_ { 0 };
	std::uint64_t traceSequence_ { 0 };
	bool flushComplete_ { false };
	std::size_t mixCountAtFlush_ { 0 };
	std::size_t coherentRun_ { 0 };
};

TEST(NetworkImpairmentCharacterizationTest, HoldAndFlushSweepProducesDeterministicBoundaryReport)
{
	const std::array<std::size_t, 6> holdSizes { 1, 2, 4, 8, 16, 32 };
	nlohmann::json summary {
		{ "scenario", "hold_and_flush" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_jitter_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "server_maximum_frames", SERVER_INCOMING_MAXIMUM_BUFFER },
		{ "results", nlohmann::json::array() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("hold-and-flush");

	for (const auto holdFrames : holdSizes) {
		for (const bool heldBeforeCurrent : { false, true }) {
			HoldFlushScenario firstScenario(holdFrames, heldBeforeCurrent);
			const auto first = firstScenario.run();
			HoldFlushScenario replayScenario(holdFrames, heldBeforeCurrent);
			const auto replay = replayScenario.run();
			EXPECT_EQ(first, replay) << "hold frames=" << holdFrames
				<< ", held before current=" << heldBeforeCurrent;
			EXPECT_GT(first.mixCount, warmupFrames);
			EXPECT_LE(first.maximumQueueA, holdFrames + BUFFER_PREFILL_ON_CONNECT + 2U);
			EXPECT_LE(first.maximumQueueB, holdFrames + BUFFER_PREFILL_ON_CONNECT + 2U);

			auto row = toJson(first);
			summary["results"].push_back(row);
			const auto traceName = "hold-" + std::to_string(holdFrames)
				+ (heldBeforeCurrent ? "-held-first.jsonl" : "-current-first.jsonl");
			firstScenario.trace().writeJsonLines(scenarioDirectory.getChildFile(traceName));
		}
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("characterization_summary", summary.dump());
}

struct ImpairmentProfile {
	ImpairmentProfile(std::string profileFamily, std::string profileName)
		: family(std::move(profileFamily)), name(std::move(profileName))
	{
	}

	std::string family;
	std::string name;
	std::uint64_t seed { 0x4a616d6d65724e65ULL };
	std::size_t fixedDelayFrames { 0 };
	std::size_t jitterFrames { 0 };
	std::size_t dropEveryFrames { 0 };
	std::size_t dropBurstFrames { 1 };
	std::size_t duplicateEveryFrames { 0 };
	std::size_t duplicateCopies { 1 };
	std::size_t reorderEveryFrames { 0 };
	std::size_t reorderDelayFrames { 0 };
	std::size_t holdEveryFrames { 0 };
	std::size_t holdFrames { 0 };
};

struct ImpairmentRunResult {
	std::string profile;
	std::size_t generatedPackets { 0 };
	std::size_t primaryPacketsDelivered { 0 };
	std::size_t injectedDrops { 0 };
	std::size_t injectedDuplicates { 0 };
	std::size_t delayedPackets { 0 };
	std::size_t rejectedPackets { 0 };
	std::size_t mixCount { 0 };
	std::size_t maximumPressureMixes { 0 };
	std::size_t singleSourceMixes { 0 };
	std::size_t skewedMixes { 0 };
	std::uint64_t maximumSourceSkew { 0 };
	std::size_t maximumQueueA { 0 };
	std::size_t maximumQueueB { 0 };
	std::size_t underrunTransitions { 0 };
	std::optional<std::size_t> recoveryMixes;
	ClientConnectionState finalStateA { ClientConnectionState::Disconnected };
	ClientConnectionState finalStateB { ClientConnectionState::Disconnected };
	std::uint64_t queueDrops { 0 };
	std::uint64_t queueDuplicates { 0 };
	std::uint64_t queueOutOfOrder { 0 };
	std::uint64_t queueTooLateOrDuplicate { 0 };
	std::uint64_t queueDropsHealed { 0 };
	std::uint64_t queueMaximumGap { 0 };
	std::uint64_t queueMaximumReorderSpan { 0 };
	ReceiverQualityResult receiverA;
	ReceiverQualityResult receiverB;

	[[nodiscard]] bool serverRecovered() const noexcept
	{
		return finalStateA == ClientConnectionState::Connected
			&& finalStateB == ClientConnectionState::Connected
			&& recoveryMixes.has_value();
	}

	[[nodiscard]] bool receiverSampleExact() const noexcept
	{
		return receiverA.sampleExact() && receiverB.sampleExact();
	}

	[[nodiscard]] bool receiversRecovered() const noexcept
	{
		return receiverA.recovered() && receiverB.recovered();
	}

	bool operator==(const ImpairmentRunResult&) const = default;
};

nlohmann::json profileJson(const ImpairmentProfile& profile)
{
	const double dropRatePercent = profile.dropEveryFrames == 0 ? 0.0
		: 100.0 * static_cast<double>(profile.dropBurstFrames)
			/ static_cast<double>(profile.dropEveryFrames);
	return {
		{ "family", profile.family },
		{ "name", profile.name },
		{ "seed", profile.seed },
		{ "fixed_delay_frames", profile.fixedDelayFrames },
		{ "fixed_delay_ms", framesToMilliseconds(profile.fixedDelayFrames) },
		{ "jitter_frames", profile.jitterFrames },
		{ "jitter_ms", framesToMilliseconds(profile.jitterFrames) },
		{ "drop_every_frames", profile.dropEveryFrames },
		{ "drop_burst_frames", profile.dropBurstFrames },
		{ "configured_drop_rate_percent", dropRatePercent },
		{ "duplicate_every_frames", profile.duplicateEveryFrames },
		{ "duplicate_copies", profile.duplicateCopies },
		{ "reorder_every_frames", profile.reorderEveryFrames },
		{ "reorder_delay_frames", profile.reorderDelayFrames },
		{ "reorder_delay_ms", framesToMilliseconds(profile.reorderDelayFrames) },
		{ "hold_every_frames", profile.holdEveryFrames },
		{ "hold_frames", profile.holdFrames },
		{ "hold_ms", framesToMilliseconds(profile.holdFrames) }
	};
}

nlohmann::json resultJson(const ImpairmentRunResult& result)
{
	const char* receiverQuality = result.receiverSampleExact()
		? "sample_exact"
		: (result.receiversRecovered() ? "glitched_but_recovered" : "persistent_failure");
	const int receiverQualityScore = result.receiverSampleExact() ? 2
		: (result.receiversRecovered() ? 1 : 0);
	return {
		{ "profile", result.profile },
		{ "server_recovered", result.serverRecovered() },
		{ "receiver_quality", receiverQuality },
		{ "receiver_quality_score", receiverQualityScore },
		{ "receiver_sample_exact", result.receiverSampleExact() },
		{ "receivers_recovered", result.receiversRecovered() },
		{ "generated_packets", result.generatedPackets },
		{ "primary_packets_delivered", result.primaryPacketsDelivered },
		{ "injected_drops", result.injectedDrops },
		{ "injected_duplicates", result.injectedDuplicates },
		{ "delayed_packets", result.delayedPackets },
		{ "rejected_packets", result.rejectedPackets },
		{ "mix_count", result.mixCount },
		{ "maximum_pressure_mixes", result.maximumPressureMixes },
		{ "single_source_mixes", result.singleSourceMixes },
		{ "skewed_mixes", result.skewedMixes },
		{ "maximum_source_skew_frames", result.maximumSourceSkew },
		{ "maximum_queue_a", result.maximumQueueA },
		{ "maximum_queue_b", result.maximumQueueB },
		{ "underrun_transitions", result.underrunTransitions },
		{ "recovery_mixes", result.recoveryMixes
			? nlohmann::json(*result.recoveryMixes) : nlohmann::json(nullptr) },
		{ "final_state_a", jammernetz::test::connectionStateName(result.finalStateA) },
		{ "final_state_b", jammernetz::test::connectionStateName(result.finalStateB) },
		{ "queue_drops", result.queueDrops },
		{ "queue_duplicates", result.queueDuplicates },
		{ "queue_out_of_order", result.queueOutOfOrder },
		{ "queue_too_late_or_duplicate", result.queueTooLateOrDuplicate },
		{ "queue_drops_healed", result.queueDropsHealed },
		{ "queue_maximum_gap", result.queueMaximumGap },
		{ "queue_maximum_reorder_span", result.queueMaximumReorderSpan },
		{ "receiver_a", receiverQualityJson(result.receiverA) },
		{ "receiver_b", receiverQualityJson(result.receiverB) }
	};
}

class ProgressiveImpairmentScenario {
public:
	explicit ProgressiveImpairmentScenario(ImpairmentProfile profile)
		: profile_(std::move(profile))
		, clientA_(std::make_shared<ClientState>("client-a"))
		, clientB_(std::make_shared<ClientState>("client-b"))
		, scheduler_(stereoMixdown(), {
			SERVER_INCOMING_JITTER_BUFFER,
			SERVER_INCOMING_MAXIMUM_BUFFER,
			BUFFER_PREFILL_ON_CONNECT
		})
		, setupA_(monoSetup(JammerNetzChannelTarget::Left))
		, setupB_(monoSetup(JammerNetzChannelTarget::Right))
		, events_(profile_.seed)
	{
		result_.profile = profile_.name;
		clients_.emplace("client-a", clientA_);
		clients_.emplace("client-b", clientB_);
	}

	ImpairmentRunResult run()
	{
		constexpr std::size_t impairmentFrames = 96;
		const std::size_t maximumDelayFrames = profile_.fixedDelayFrames
			+ profile_.jitterFrames + profile_.reorderDelayFrames + profile_.holdFrames;
		const std::uint64_t firstImpairedFrame = warmupFrames + 1U;
		const std::uint64_t firstRecoveryFrame = firstImpairedFrame + impairmentFrames;
		const std::uint64_t finalFrame = firstRecoveryFrame + recoveryFrames
			+ maximumDelayFrames;
		recoveryEligibleSample_ = (firstRecoveryFrame + maximumDelayFrames + 1U)
			* static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE);
		const auto receiverRecoverySample = recoveryEligibleSample_
			+ static_cast<SampleIndex>(CLIENT_PLAYOUT_JITTER_BUFFER * SAMPLE_BUFFER_SIZE);
		receiverA_ = std::make_unique<ReceiverQualityProbe>("client-a", receiverRecoverySample,
			finalFrame + maximumDelayFrames);
		receiverB_ = std::make_unique<ReceiverQualityProbe>("client-b", receiverRecoverySample,
			finalFrame + maximumDelayFrames);

		for (std::uint64_t frame = 0; frame <= finalFrame; ++frame) {
			scheduleDelivery(0, frame, 0, "paced", false);
			++result_.generatedPackets;
			const bool impaired = frame >= firstImpairedFrame && frame < firstRecoveryFrame;
			if (!impaired) {
				scheduleDelivery(1, frame, 0, "paced", false);
				continue;
			}

			const std::size_t relativeFrame = static_cast<std::size_t>(frame - firstImpairedFrame);
			if (shouldDrop(relativeFrame)) {
				scheduleDrop(frame);
				continue;
			}

			const std::size_t delayFrames = deliveryDelay(relativeFrame);
			const std::string action = delayFrames == 0 ? "paced" : "delayed";
			scheduleDelivery(1, frame, delayFrames, action, false);
			if (profile_.duplicateEveryFrames > 0
				&& relativeFrame % profile_.duplicateEveryFrames == 0) {
				for (std::size_t copy = 0; copy < profile_.duplicateCopies; ++copy) {
					scheduleDelivery(1, frame, delayFrames, "duplicated", true, copy + 1U);
				}
			}
		}
		for (std::uint64_t frame = 0; frame <= finalFrame; ++frame) {
			const auto callbackSample = frame * static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE);
			events_.scheduleAt(callbackSample, "receiver_audio_callback", [this, callbackSample] {
				receiverA_->processCallback(callbackSample);
				receiverB_->processCallback(callbackSample);
			}, { { "frame", frame } });
		}

		events_.runUntilIdle();
		const auto snapshotA = clientA_->snapshot();
		const auto snapshotB = clientB_->snapshot();
		result_.finalStateA = snapshotA.state;
		result_.finalStateB = snapshotB.state;
		JammerNetzStreamQualityInfo quality;
		if (clientB_->qualityInfo(quality)) {
			result_.queueDrops = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.droppedPacketCounter));
			result_.queueDuplicates = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.duplicatePacketCounter));
			result_.queueOutOfOrder = static_cast<std::uint64_t>(std::max<std::int64_t>(0, quality.outOfOrderPacketCounter));
			result_.queueTooLateOrDuplicate = quality.tooLateOrDuplicate;
			result_.queueDropsHealed = quality.dropsHealed;
			result_.queueMaximumGap = quality.maxLengthOfGap;
			result_.queueMaximumReorderSpan = quality.maxWrongOrderSpan;
		}
		result_.receiverA = receiverA_->finish();
		result_.receiverB = receiverB_->finish();
		return result_;
	}

	[[nodiscard]] const ScenarioTrace& trace() const noexcept { return trace_; }

private:
	[[nodiscard]] bool shouldDrop(const std::size_t relativeFrame) const noexcept
	{
		return profile_.dropEveryFrames > 0
			&& relativeFrame % profile_.dropEveryFrames < profile_.dropBurstFrames;
	}

	[[nodiscard]] std::size_t deliveryDelay(const std::size_t relativeFrame) const noexcept
	{
		std::size_t delay = profile_.fixedDelayFrames;
		if (profile_.jitterFrames > 0) {
			const std::uint64_t mixed = (static_cast<std::uint64_t>(relativeFrame) + 1U)
				* 0x9e3779b97f4a7c15ULL + profile_.seed;
			delay += static_cast<std::size_t>((mixed ^ (mixed >> 29U))
				% (profile_.jitterFrames + 1U));
		}
		if (profile_.reorderEveryFrames > 0 && relativeFrame % profile_.reorderEveryFrames == 0) {
			delay += profile_.reorderDelayFrames;
		}
		if (profile_.holdEveryFrames > 0 && profile_.holdFrames > 0) {
			const std::size_t holdPosition = relativeFrame % profile_.holdEveryFrames;
			if (holdPosition < profile_.holdFrames) {
				delay += profile_.holdFrames - holdPosition;
			}
		}
		return delay;
	}

	[[nodiscard]] ClientState::TimePoint now() const
	{
		const auto micros = static_cast<std::int64_t>(events_.now() * 1000000ULL / SAMPLE_RATE);
		return ClientState::TimePoint{} + std::chrono::microseconds(micros);
	}

	void scheduleDrop(const std::uint64_t frame)
	{
		const auto sample = frame * static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE);
		events_.scheduleAt(sample, "packet_drop", [this, frame] {
			++result_.injectedDrops;
			record("delivery", {
				{ "client", "client-b" }, { "frame", frame }, { "action", "dropped" }
			});
		}, { { "client", "client-b" }, { "frame", frame } });
	}

	void scheduleDelivery(const std::size_t clientIndex,
		const std::uint64_t frame,
		const std::size_t delayFrames,
		const std::string& action,
		const bool duplicate,
		const std::size_t duplicateOffsetSamples = 0)
	{
		const auto& setup = clientIndex == 0 ? setupA_ : setupB_;
		auto packet = makePacket(static_cast<std::uint32_t>(clientIndex + 1U), frame, setup);
		const auto deliverySample = (frame + delayFrames)
			* static_cast<std::uint64_t>(SAMPLE_BUFFER_SIZE) + duplicateOffsetSamples;
		events_.scheduleAt(deliverySample, "packet_delivery",
			[this, clientIndex, frame, capturedPacket = std::move(packet), action, duplicate, delayFrames] {
				if (duplicate) {
					++result_.injectedDuplicates;
				}
				else if (clientIndex == 1U) {
					++result_.primaryPacketsDelivered;
				}
				if (clientIndex == 1U && delayFrames > 0) {
					++result_.delayedPackets;
				}
				deliver(clientIndex, frame, capturedPacket, action);
			}, {
				{ "client", clientIndex == 0 ? "client-a" : "client-b" },
				{ "frame", frame }, { "action", action }, { "delay_frames", delayFrames }
			});
	}

	void deliver(const std::size_t clientIndex,
		const std::uint64_t frame,
		const std::shared_ptr<JammerNetzAudioData>& packet,
		const std::string& action)
	{
		auto& client = clientIndex == 0 ? clientA_ : clientB_;
		const auto push = client->push(packet, BUFFER_PREFILL_ON_CONNECT, now());
		record("delivery", {
			{ "client", clientIndex == 0 ? "client-a" : "client-b" },
			{ "frame", frame }, { "action", action }, { "queued", push.queued }
		});
		if (!push.queued) {
			++result_.rejectedPackets;
			return;
		}
		// Register both initial clients before the first mixer wake-up. Otherwise
		// client A is briefly treated as a one-client session and loses one
		// prefill packet before client B exists, invalidating the clean control.
		if (frame == 0 && clientIndex == 0) {
			return;
		}
		processWakeups();
	}

	void processWakeups()
	{
		constexpr std::size_t maximumSteps = 4096;
		std::size_t steps = 0;
		bool wakeAgain = true;
		while (wakeAgain) {
			if (++steps > maximumSteps) {
				throw std::runtime_error("The impairment scheduler kept requesting wake-ups without settling");
			}
			const auto step = scheduler_.process(clients_, now());
			recordStep(step);
			wakeAgain = step.shouldWakeAgain;
		}
	}

	void record(const std::string& kind, nlohmann::json details)
	{
		trace_.record({ events_.now(), traceSequence_++, kind, std::move(details) });
	}

	void recordStep(const ServerScheduledMixResult& step)
	{
		if (step.mix.outgoing.size() != step.incoming.size()) {
			throw std::runtime_error("The server mixer did not produce one routed packet per impairment input");
		}
		if (!step.mix.diagnostics.empty()) {
			throw std::runtime_error("The impairment input produced an invalid mixer diagnostic");
		}
		for (const auto& [name, queue] : step.queuesBefore) {
			if (name == "client-a") {
				result_.maximumQueueA = std::max(result_.maximumQueueA, queue.size);
			}
			else if (name == "client-b") {
				result_.maximumQueueB = std::max(result_.maximumQueueB, queue.size);
			}
		}
		result_.underrunTransitions += step.underrunClients.size();
		if (step.trigger == ServerMixTrigger::MaximumBufferPressure
			|| step.trigger == ServerMixTrigger::AllClientsReadyAndMaximumBufferPressure) {
			++result_.maximumPressureMixes;
		}

		nlohmann::json selected = nlohmann::json::object();
		for (const auto& [name, packet] : step.incoming) {
			selected[name] = packet->messageCounter();
		}
		record("server_mix_decision", {
			{ "trigger", triggerName(step.trigger) },
			{ "queues_before", queueJson(step.queuesBefore) },
			{ "queues_after", queueJson(step.queuesAfter) },
			{ "selected_counters", selected },
			{ "underrun_clients", step.underrunClients }
		});

		if (step.incoming.empty()) {
			return;
		}
		for (const auto& outgoing : step.mix.outgoing) {
			if (outgoing.targetAddress == "client-a") {
				receiverA_->deliver(outgoing);
			}
			else if (outgoing.targetAddress == "client-b") {
				receiverB_->deliver(outgoing);
			}
		}
		++result_.mixCount;
		bool coherent = false;
		if (step.incoming.size() == 1U) {
			++result_.singleSourceMixes;
		}
		else if (step.incoming.size() == 2U) {
			const auto counterA = static_cast<std::uint64_t>(
				step.incoming.at("client-a")->messageCounter());
			const auto counterB = static_cast<std::uint64_t>(
				step.incoming.at("client-b")->messageCounter());
			const auto skew = counterA > counterB ? counterA - counterB : counterB - counterA;
			result_.maximumSourceSkew = std::max(result_.maximumSourceSkew, skew);
			coherent = skew == 0;
			if (!coherent) {
				++result_.skewedMixes;
			}
		}

		if (events_.now() < recoveryEligibleSample_ || result_.recoveryMixes) {
			return;
		}
		if (recoveryObservationMix_ == 0) {
			recoveryObservationMix_ = result_.mixCount;
		}
		coherentRun_ = coherent ? coherentRun_ + 1U : 0U;
		if (coherentRun_ >= coherentRecoveryWindow) {
			result_.recoveryMixes = result_.mixCount - recoveryObservationMix_ + 1U;
		}
	}

	ImpairmentProfile profile_;
	ImpairmentRunResult result_;
	TPacketStreamBundle clients_;
	std::shared_ptr<ClientState> clientA_;
	std::shared_ptr<ClientState> clientB_;
	ServerMixScheduler scheduler_;
	JammerNetzChannelSetup setupA_;
	JammerNetzChannelSetup setupB_;
	jammernetz::test::ScenarioScheduler events_;
	ScenarioTrace trace_;
	std::uint64_t traceSequence_ { 0 };
	SampleIndex recoveryEligibleSample_ { 0 };
	std::size_t recoveryObservationMix_ { 0 };
	std::size_t coherentRun_ { 0 };
	std::unique_ptr<ReceiverQualityProbe> receiverA_;
	std::unique_ptr<ReceiverQualityProbe> receiverB_;
};

struct ServerRecoveryBoundary {
	std::optional<std::string> lastRecovered;
	std::optional<std::string> firstFailure;
};

struct ReceiverQualityBoundary {
	std::optional<std::string> lastSampleExact;
	std::optional<std::string> firstGlitch;
	std::optional<std::string> firstPersistentFailure;
};

nlohmann::json boundaryJson(const ServerRecoveryBoundary& boundary)
{
	return {
		{ "last_server_recovered_profile", boundary.lastRecovered
			? nlohmann::json(*boundary.lastRecovered) : nlohmann::json(nullptr) },
		{ "first_server_recovery_failure_profile", boundary.firstFailure
			? nlohmann::json(*boundary.firstFailure) : nlohmann::json(nullptr) }
	};
}

nlohmann::json boundaryJson(const ReceiverQualityBoundary& boundary)
{
	return {
		{ "last_receiver_sample_exact_profile", boundary.lastSampleExact
			? nlohmann::json(*boundary.lastSampleExact) : nlohmann::json(nullptr) },
		{ "first_receiver_glitch_profile", boundary.firstGlitch
			? nlohmann::json(*boundary.firstGlitch) : nlohmann::json(nullptr) },
		{ "first_receiver_persistent_failure_profile", boundary.firstPersistentFailure
			? nlohmann::json(*boundary.firstPersistentFailure) : nlohmann::json(nullptr) }
	};
}

void observeReceiverBoundary(ReceiverQualityBoundary& boundary,
	const ImpairmentRunResult& result)
{
	if (result.receiverSampleExact() && !boundary.firstGlitch) {
		boundary.lastSampleExact = result.profile;
	}
	else if (!result.receiverSampleExact() && !boundary.firstGlitch) {
		boundary.firstGlitch = result.profile;
	}
	if (!result.receiversRecovered() && !boundary.firstPersistentFailure) {
		boundary.firstPersistentFailure = result.profile;
	}
}

ImpairmentRunResult runAndRecord(const ImpairmentProfile& profile,
	const juce::File& traceDirectory,
	nlohmann::json& rows)
{
	ProgressiveImpairmentScenario firstScenario(profile);
	const auto first = firstScenario.run();
	ProgressiveImpairmentScenario replayScenario(profile);
	const auto replay = replayScenario.run();
	EXPECT_EQ(first, replay) << profile.name;
	EXPECT_EQ(first.generatedPackets, first.primaryPacketsDelivered + first.injectedDrops)
		<< profile.name;
	EXPECT_GT(first.mixCount, warmupFrames) << profile.name;
	if (profile.dropEveryFrames > 0) {
		EXPECT_GT(first.injectedDrops, 0U) << profile.name;
	}
	if (profile.duplicateEveryFrames > 0) {
		EXPECT_GT(first.injectedDuplicates, 0U) << profile.name;
	}
	if (profile.fixedDelayFrames > 0 || profile.jitterFrames > 0
		|| profile.reorderDelayFrames > 0 || profile.holdFrames > 0) {
		EXPECT_GT(first.delayedPackets, 0U) << profile.name;
	}
	firstScenario.trace().writeJsonLines(traceDirectory.getChildFile(profile.name + ".jsonl"));
	rows.push_back({ { "profile", profileJson(profile) }, { "result", resultJson(first) } });
	return first;
}

std::vector<ImpairmentProfile> isolatedProfiles()
{
	std::vector<ImpairmentProfile> profiles;
	profiles.push_back({ "control", "clean" });
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "lag", "lag-" + std::to_string(frames) + "-frames" };
		profile.fixedDelayFrames = frames;
		profiles.push_back(profile);
	}
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "jitter", "jitter-0-to-" + std::to_string(frames) + "-frames" };
		profile.jitterFrames = frames;
		profiles.push_back(profile);
	}
	for (const std::size_t every : { 64U, 32U, 16U, 8U, 4U, 2U }) {
		ImpairmentProfile profile { "drop-rate", "drop-every-" + std::to_string(every) };
		profile.dropEveryFrames = every;
		profiles.push_back(profile);
	}
	for (const std::size_t burst : { 1U, 2U, 3U, 4U, 8U }) {
		ImpairmentProfile profile { "drop-burst", "drop-burst-" + std::to_string(burst) };
		profile.dropEveryFrames = 32;
		profile.dropBurstFrames = burst;
		profiles.push_back(profile);
	}
	for (const std::size_t every : { 64U, 32U, 16U, 8U, 4U, 2U }) {
		ImpairmentProfile profile { "duplicate", "duplicate-every-" + std::to_string(every) };
		profile.duplicateEveryFrames = every;
		profiles.push_back(profile);
	}
	for (const std::size_t displacement : { 1U, 2U, 4U, 8U, 16U }) {
		ImpairmentProfile profile { "reorder", "reorder-" + std::to_string(displacement) + "-frames" };
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	for (const std::size_t frames : { 1U, 2U, 4U, 8U, 16U, 32U }) {
		ImpairmentProfile profile { "throttle", "throttle-hold-" + std::to_string(frames)
			+ "-every-32" };
		profile.holdEveryFrames = 32;
		profile.holdFrames = frames;
		profiles.push_back(profile);
	}
	return profiles;
}

std::vector<ImpairmentProfile> combinedProfiles()
{
	std::vector<ImpairmentProfile> profiles;
	for (const auto& [jitter, dropEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 64U }, std::pair { 4U, 32U }, std::pair { 8U, 16U }
	}) {
		ImpairmentProfile profile { "jitter+drop", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profiles.push_back(profile);
	}
	for (const auto& [jitter, displacement] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 1U }, std::pair { 4U, 2U }, std::pair { 8U, 4U }
	}) {
		ImpairmentProfile profile { "jitter+reorder", "jitter-" + std::to_string(jitter)
			+ "_reorder-" + std::to_string(displacement) };
		profile.jitterFrames = jitter;
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	for (const auto& [hold, duplicateEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 16U }, std::pair { 4U, 8U }, std::pair { 8U, 4U }
	}) {
		ImpairmentProfile profile { "hold+duplicate", "hold-" + std::to_string(hold)
			+ "_duplicate-every-" + std::to_string(duplicateEvery) };
		profile.holdEveryFrames = 32;
		profile.holdFrames = hold;
		profile.duplicateEveryFrames = duplicateEvery;
		profiles.push_back(profile);
	}
	for (const auto& [lag, dropEvery] : std::array<std::pair<std::size_t, std::size_t>, 3> {
		std::pair { 2U, 32U }, std::pair { 4U, 16U }, std::pair { 8U, 8U }
	}) {
		ImpairmentProfile profile { "lag+drop", "lag-" + std::to_string(lag)
			+ "_drop-every-" + std::to_string(dropEvery) };
		profile.fixedDelayFrames = lag;
		profile.dropEveryFrames = dropEvery;
		profiles.push_back(profile);
	}
	for (const auto& [jitter, dropEvery, duplicateEvery] :
		std::array<std::tuple<std::size_t, std::size_t, std::size_t>, 3> {
			std::tuple { 2U, 64U, 32U }, std::tuple { 4U, 32U, 16U }, std::tuple { 8U, 16U, 8U }
		}) {
		ImpairmentProfile profile { "jitter+drop+duplicate", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery)
			+ "_duplicate-every-" + std::to_string(duplicateEvery) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profile.duplicateEveryFrames = duplicateEvery;
		profiles.push_back(profile);
	}
	for (const auto& [jitter, dropEvery, displacement] :
		std::array<std::tuple<std::size_t, std::size_t, std::size_t>, 3> {
			std::tuple { 2U, 64U, 1U }, std::tuple { 4U, 32U, 2U }, std::tuple { 8U, 16U, 4U }
		}) {
		ImpairmentProfile profile { "jitter+drop+reorder", "jitter-" + std::to_string(jitter)
			+ "_drop-every-" + std::to_string(dropEvery)
			+ "_reorder-" + std::to_string(displacement) };
		profile.jitterFrames = jitter;
		profile.dropEveryFrames = dropEvery;
		profile.reorderEveryFrames = 8;
		profile.reorderDelayFrames = displacement;
		profiles.push_back(profile);
	}
	return profiles;
}

std::vector<ImpairmentProfile> qualitySurfaceProfiles(const std::size_t holdFrames)
{
	std::vector<ImpairmentProfile> profiles;
	for (const std::size_t jitter : { 0U, 2U, 4U, 6U, 8U }) {
		for (const std::size_t dropEvery : { 0U, 64U, 32U, 16U, 8U, 4U, 2U }) {
			const std::string dropName = dropEvery == 0
				? "none" : "every-" + std::to_string(dropEvery);
			ImpairmentProfile profile {
				"quality-surface-hold-" + std::to_string(holdFrames),
				"jitter-" + std::to_string(jitter) + "_drop-" + dropName
					+ "_hold-" + std::to_string(holdFrames)
			};
			profile.jitterFrames = jitter;
			profile.dropEveryFrames = dropEvery;
			if (holdFrames > 0) {
				profile.holdEveryFrames = 32;
				profile.holdFrames = holdFrames;
			}
			profiles.push_back(std::move(profile));
		}
	}
	return profiles;
}

void runQualitySurfaceFacet(const std::size_t holdFrames)
{
	nlohmann::json summary {
		{ "scenario", "receiver_quality_surface" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "frame_ms", framesToMilliseconds(1) },
		{ "hold_frames", holdFrames },
		{ "hold_ms", framesToMilliseconds(holdFrames) },
		{ "axis_definition", {
			{ "x", "jitter_frames" },
			{ "y", "configured_drop_rate_percent" },
			{ "facet", "hold_frames" },
			{ "quality", "receiver_quality" }
		} },
		{ "quality_classes", {
			{ "sample_exact", { { "score", 2 }, { "definition", "both receivers rendered every post-start sample exactly with no underrun" } } },
			{ "glitched_but_recovered", { { "score", 1 }, { "definition", "at least one receiver glitched, then both rendered eight identifiable sequential frames" } } },
			{ "persistent_failure", { { "score", 0 }, { "definition", "at least one receiver did not regain eight identifiable sequential frames" } } }
		} },
		{ "jitter_axis_frames", { 0, 2, 4, 6, 8 } },
		{ "drop_every_axis_frames", { 0, 64, 32, 16, 8, 4, 2 } },
		{ "results", nlohmann::json::array() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("network-impairments")
		.getChildFile("quality-surface")
		.getChildFile("hold-" + std::to_string(holdFrames));
	for (const auto& profile : qualitySurfaceProfiles(holdFrames)) {
		runAndRecord(profile, scenarioDirectory, summary["results"]);
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
}

TEST(NetworkImpairmentCharacterizationTest, IsolatedClumsyStyleSweepsReportServerRecoveryBoundaries)
{
	nlohmann::json summary {
		{ "scenario", "isolated_progressive_impairments" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_jitter_frames", SERVER_INCOMING_JITTER_BUFFER },
		{ "server_maximum_frames", SERVER_INCOMING_MAXIMUM_BUFFER },
		{ "server_recovery_definition", "both server-side client states connected and eight consecutive coherent server mixes after impairment" },
		{ "receiver_quality_definition", "sample-exact compares every rendered receiver sample after playout starts with the ideal sequential two-client mix; recovered requires eight consecutive identifiable sequential receiver frames after impairment" },
		{ "results", nlohmann::json::array() },
		{ "boundaries", nlohmann::json::object() },
		{ "receiver_boundaries", nlohmann::json::object() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("network-impairments").getChildFile("isolated");
	std::map<std::string, ServerRecoveryBoundary> boundaries;
	std::map<std::string, ReceiverQualityBoundary> receiverBoundaries;
	for (const auto& profile : isolatedProfiles()) {
		const auto result = runAndRecord(profile, scenarioDirectory, summary["results"]);
		if (profile.family == "control") {
			EXPECT_TRUE(result.receiverSampleExact());
			EXPECT_EQ(result.receiverA.playoutUnderruns, 0U);
			EXPECT_EQ(result.receiverB.playoutUnderruns, 0U);
		}
		auto& boundary = boundaries[profile.family];
		if (result.serverRecovered() && !boundary.firstFailure) {
			boundary.lastRecovered = profile.name;
		}
		else if (!result.serverRecovered() && !boundary.firstFailure) {
			boundary.firstFailure = profile.name;
		}
		observeReceiverBoundary(receiverBoundaries[profile.family], result);
	}
	for (const auto& [family, boundary] : boundaries) {
		summary["boundaries"][family] = boundaryJson(boundary);
	}
	for (const auto& [family, boundary] : receiverBoundaries) {
		summary["receiver_boundaries"][family] = boundaryJson(boundary);
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("isolated_impairment_summary", summary.dump());
}

TEST(NetworkImpairmentCharacterizationTest, CombinedClumsyStyleProfilesRemainDeterministic)
{
	nlohmann::json summary {
		{ "scenario", "combined_impairments" },
		{ "sample_rate", SAMPLE_RATE },
		{ "frame_samples", SAMPLE_BUFFER_SIZE },
		{ "server_recovery_definition", "both server-side client states connected and eight consecutive coherent server mixes after impairment" },
		{ "receiver_quality_definition", "sample-exact compares every rendered receiver sample after playout starts with the ideal sequential two-client mix; recovered requires eight consecutive identifiable sequential receiver frames after impairment" },
		{ "results", nlohmann::json::array() },
		{ "boundaries", nlohmann::json::object() },
		{ "receiver_boundaries", nlohmann::json::object() }
	};
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const auto scenarioDirectory = artifactDirectory.getChildFile("network-impairments").getChildFile("combined");
	std::map<std::string, ServerRecoveryBoundary> boundaries;
	std::map<std::string, ReceiverQualityBoundary> receiverBoundaries;
	for (const auto& profile : combinedProfiles()) {
		const auto result = runAndRecord(profile, scenarioDirectory, summary["results"]);
		auto& boundary = boundaries[profile.family];
		if (result.serverRecovered() && !boundary.firstFailure) {
			boundary.lastRecovered = profile.name;
		}
		else if (!result.serverRecovered() && !boundary.firstFailure) {
			boundary.firstFailure = profile.name;
		}
		observeReceiverBoundary(receiverBoundaries[profile.family], result);
	}
	for (const auto& [family, boundary] : boundaries) {
		summary["boundaries"][family] = boundaryJson(boundary);
	}
	for (const auto& [family, boundary] : receiverBoundaries) {
		summary["receiver_boundaries"][family] = boundaryJson(boundary);
	}
	jammernetz::test::writeJsonArtifact(scenarioDirectory.getChildFile("summary.json"),
		summary, "characterization");
	RecordProperty("combined_impairment_summary", summary.dump());
}

TEST(NetworkImpairmentCharacterizationTest, ReceiverQualitySurfaceWithoutSlottingIsDeterministic)
{
	runQualitySurfaceFacet(0);
}

TEST(NetworkImpairmentCharacterizationTest, ReceiverQualitySurfaceWithTwoFrameSlottingIsDeterministic)
{
	runQualitySurfaceFacet(2);
}

TEST(NetworkImpairmentCharacterizationTest, ReceiverQualitySurfaceWithFourFrameSlottingIsDeterministic)
{
	runQualitySurfaceFacet(4);
}

TEST(NetworkImpairmentCharacterizationTest, ReceiverQualitySurfaceWithEightFrameSlottingIsDeterministic)
{
	runQualitySurfaceFacet(8);
}

} // namespace
