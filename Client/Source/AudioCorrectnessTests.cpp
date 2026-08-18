#include "AudioCorrectness.h"
#include "PathMtuDiscovery.h"

#include <gtest/gtest.h>

TEST(AudioCorrectness, RejectsAnEmptyBufferSizeList)
{
	EXPECT_FALSE(AudioCorrectness::selectBufferSize({}).has_value());
}

TEST(AudioCorrectness, SelectsSmallestSizeAtOrAbovePreferredMinimum)
{
	EXPECT_EQ(AudioCorrectness::selectBufferSize({ 1024, 64, 256, 128 }), 128);
	EXPECT_EQ(AudioCorrectness::selectBufferSize({ 1024, 512, 256 }), 256);
}

TEST(AudioCorrectness, FallsBackToLargestPositiveSizeBelowPreferredMinimum)
{
	EXPECT_EQ(AudioCorrectness::selectBufferSize({ 0, -1, 32, 64 }), 64);
}

TEST(AudioCorrectness, RejectsAListWithoutPositiveSizes)
{
	EXPECT_FALSE(AudioCorrectness::selectBufferSize({ 0, -1, -256 }).has_value());
}

TEST(AudioCorrectness, RequiresInputAndOutputChannels)
{
	EXPECT_TRUE(AudioCorrectness::hasUsableChannelSelection({ 0 }, { 0, 1 }));
	EXPECT_FALSE(AudioCorrectness::hasUsableChannelSelection({}, { 0, 1 }));
	EXPECT_FALSE(AudioCorrectness::hasUsableChannelSelection({ 0 }, {}));
}

TEST(PathMtuDiscoveryTest, SendsNothingUntilTheServerAdvertisesSupport)
{
	PathMtuDiscovery discovery;
	EXPECT_EQ(discovery.status(), PathMtuDiscoveryStatus::Unavailable);
	EXPECT_FALSE(discovery.poll(PathMtuDiscovery::TimePoint {}).has_value());
	EXPECT_EQ(discovery.safePayloadBytes(), PathMtuDiscovery::fallbackPayloadBytes);
}

TEST(PathMtuDiscoveryTest, FindsAJumboPayloadWithRetriesAndBinarySearch)
{
	PathMtuDiscovery discovery;
	discovery.setSupported(true);
	auto now = PathMtuDiscovery::TimePoint {};
	constexpr int supportedPayloadBytes = 8968;

	for (int step = 0; step < 100 && discovery.status() == PathMtuDiscoveryStatus::Searching; ++step) {
		const auto probe = discovery.poll(now);
		if (!probe.has_value()) {
			now += PathMtuDiscovery::probeTimeout;
			continue;
		}
		if (probe->payloadBytes <= supportedPayloadBytes) {
			EXPECT_TRUE(discovery.acknowledge(probe->id, probe->payloadBytes, now));
			now += PathMtuDiscovery::probeTimeout;
		} else {
			now += PathMtuDiscovery::probeTimeout;
		}
	}

	EXPECT_EQ(discovery.status(), PathMtuDiscoveryStatus::Complete);
	EXPECT_EQ(discovery.safePayloadBytes(), supportedPayloadBytes);
}

TEST(PathMtuDiscoveryTest, IgnoresAnAcknowledgementForAnotherProbe)
{
	PathMtuDiscovery discovery;
	discovery.setSupported(true);
	const auto probe = discovery.poll(PathMtuDiscovery::TimePoint {});
	ASSERT_TRUE(probe.has_value());
	EXPECT_FALSE(discovery.acknowledge(probe->id + 1, probe->payloadBytes));
	EXPECT_EQ(discovery.status(), PathMtuDiscoveryStatus::Searching);
}
