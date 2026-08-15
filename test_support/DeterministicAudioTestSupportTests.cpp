/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DeterministicAudioTestSupport.h"

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

namespace jammernetz::test {
namespace {

CapturedAudio capture(juce::AudioBuffer<float> buffer)
{
	CapturedAudio result;
	result.append(buffer);
	return result;
}

TEST(SyntheticAudioSourceTest, IsRepeatableAcrossCallbackBoundaries)
{
	SyntheticAudioSource wholeSource(17, 2, 4096);
	SyntheticAudioSource splitSource(17, 2, 4096);

	CapturedAudio whole;
	whole.append(wholeSource.render(128));
	CapturedAudio split;
	split.append(splitSource.render(32));
	split.append(splitSource.render(96));

	EXPECT_EQ(wholeSource.nextSample(), 4224U);
	EXPECT_EQ(splitSource.nextSample(), 4224U);
	EXPECT_TRUE(SignalOracle::compare(whole, split, 0.0f).empty());
}

TEST(SyntheticAudioSourceTest, EncodesSourceAndChannelIdentity)
{
	const auto sourceOne = capture(SyntheticAudioSource(1, 2).render(128));
	const auto sourceTwo = capture(SyntheticAudioSource(2, 2).render(128));

	const auto discrepancies = SignalOracle::compare(sourceOne, sourceTwo, 0.0f);
	ASSERT_FALSE(discrepancies.empty());
	EXPECT_EQ(discrepancies.front().firstSample, 0U);
}

TEST(ScenarioSchedulerTest, ReplaysSeedAndSameTimeOrderingExactly)
{
	auto runScenario = []() {
		ScenarioScheduler scheduler(0x12345678U);
		std::vector<std::string> order;
		std::vector<std::uint64_t> randomValues;
		scheduler.scheduleAt(128, "produce", [&] {
			order.emplace_back("first-at-128");
			randomValues.push_back(scheduler.nextRandom());
		});
		scheduler.scheduleAt(64, "deliver", [&] {
			order.emplace_back("at-64");
			randomValues.push_back(scheduler.nextRandom());
		});
		scheduler.scheduleAt(128, "mix", [&] {
			order.emplace_back("second-at-128");
			randomValues.push_back(scheduler.nextRandom());
		});
		scheduler.runUntilIdle();
		return std::make_tuple(order, randomValues, scheduler.trace().toJsonLines());
	};

	const auto first = runScenario();
	const auto second = runScenario();
	EXPECT_EQ(first, second);
	EXPECT_EQ(std::get<0>(first), (std::vector<std::string> { "at-64", "first-at-128", "second-at-128" }));
}

TEST(SignalOracleTest, CoalescesAdjacentDifferencesIntoSpans)
{
	juce::AudioBuffer<float> expectedBuffer(1, 16);
	expectedBuffer.clear();
	juce::AudioBuffer<float> observedBuffer(expectedBuffer);
	for (int sample = 4; sample <= 6; ++sample) {
		observedBuffer.setSample(0, sample, 0.25f);
	}
	observedBuffer.setSample(0, 10, -0.5f);

	const auto discrepancies = SignalOracle::compare(
		capture(expectedBuffer), capture(observedBuffer), 1.0e-6f, 1000);
	ASSERT_EQ(discrepancies.size(), 2U);
	EXPECT_EQ(discrepancies[0].channel, 0);
	EXPECT_EQ(discrepancies[0].firstSample, 1004U);
	EXPECT_EQ(discrepancies[0].lastSample, 1006U);
	EXPECT_FLOAT_EQ(discrepancies[0].maximumAbsoluteError, 0.25f);
	EXPECT_EQ(discrepancies[1].firstSample, 1010U);
	EXPECT_EQ(discrepancies[1].lastSample, 1010U);
	EXPECT_FLOAT_EQ(discrepancies[1].observedAtFirst, -0.5f);
}

TEST(ScenarioTraceTest, WritesDeterministicJsonLinesUnderTheBuildTree)
{
	ScenarioTrace trace;
	trace.record(TraceEvent { 128, 7, "deliver", { { "client", "alice" }, { "queue_size", 3 } } });
	const juce::File artifactDirectory(JAMMERNETZ_TEST_ARTIFACT_DIR);
	const juce::File traceDirectory = artifactDirectory.getChildFile("trace-writer-test");
	const juce::File tracePath = traceDirectory.getChildFile("trace.jsonl");
	trace.writeJsonLines(tracePath);

	auto input = tracePath.createInputStream();
	ASSERT_NE(input, nullptr);
	const std::string written = input->readEntireStreamAsString().toStdString();
	EXPECT_EQ(written, trace.toJsonLines());
	const nlohmann::json document = nlohmann::json::parse(written);
	EXPECT_EQ(document.at("virtual_sample"), 128U);
	EXPECT_EQ(document.at("sequence"), 7U);
	EXPECT_EQ(document.at("kind"), "deliver");
	EXPECT_EQ(document.at("details").at("client"), "alice");

	input.reset();
	EXPECT_TRUE(traceDirectory.deleteRecursively());
}

} // namespace
} // namespace jammernetz::test
