#include "AudioCorrectness.h"

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
