/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "StreamingAudioResampler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {

std::vector<float> sineWave(const int samples, const double sampleRate,
	const double frequency)
{
	std::vector<float> result(static_cast<std::size_t>(samples));
	for (int sample = 0; sample < samples; ++sample) {
		result[static_cast<std::size_t>(sample)] = static_cast<float>(std::sin(
			2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate));
	}
	return result;
}

double rmsError(const std::vector<float>& actual, const std::vector<float>& expected,
	const std::size_t samples)
{
	double sum = 0.0;
	for (std::size_t sample = 0; sample < samples; ++sample) {
		const auto difference = static_cast<double>(actual[sample] - expected[sample]);
		sum += difference * difference;
	}
	return std::sqrt(sum / static_cast<double>(samples));
}

} // namespace

TEST(StreamingAudioResamplerTest, UnityRateIsSampleExact)
{
	auto input = sineWave(4096, 48000.0, 997.0);
	std::vector<float> output(input.size());
	const float* inputs[] { input.data() };
	float* outputs[] { output.data() };
	StreamingAudioResampler resampler;
	ASSERT_TRUE(resampler.prepare(1, 0.9, 1.1));

	const auto result = resampler.process(inputs, static_cast<int>(input.size()), outputs,
		static_cast<int>(output.size()), 1.0, true);

	EXPECT_EQ(result.inputSamplesUsed, input.size());
	EXPECT_EQ(result.outputSamplesGenerated, input.size());
	EXPECT_EQ(output, input);
}

TEST(StreamingAudioResamplerTest, Converts44100To48000WithoutChangingPitch)
{
	constexpr int inputRate = 44100;
	constexpr int outputRate = 48000;
	constexpr double frequency = 997.0;
	auto input = sineWave(inputRate, inputRate, frequency);
	std::vector<float> output(outputRate + 32);
	const float* inputs[] { input.data() };
	float* outputs[] { output.data() };
	StreamingAudioResampler resampler;
	const auto factor = static_cast<double>(outputRate) / inputRate;
	ASSERT_TRUE(resampler.prepare(1, 0.9, 1.2));

	const auto result = resampler.process(inputs, inputRate, outputs,
		static_cast<int>(output.size()), factor, true);

	EXPECT_EQ(result.inputSamplesUsed, inputRate);
	EXPECT_NEAR(result.outputSamplesGenerated, outputRate, 3);
	const auto expected = sineWave(result.outputSamplesGenerated, outputRate, frequency);
	const auto compared = static_cast<std::size_t>(std::max(0,
		result.outputSamplesGenerated - resampler.filterWidth()));
	ASSERT_GT(compared, 40000U);
	EXPECT_LT(rmsError(output, expected, compared), 1.0e-3);
}

TEST(StreamingAudioResamplerTest, CorrectsMeasured47850HzHardwareClock)
{
	constexpr int inputRate = 47850;
	constexpr int outputRate = 48000;
	auto input = sineWave(inputRate, inputRate, 440.0);
	std::vector<float> output(outputRate + 32);
	const float* inputs[] { input.data() };
	float* outputs[] { output.data() };
	StreamingAudioResampler resampler;
	const auto factor = static_cast<double>(outputRate) / inputRate;
	ASSERT_TRUE(resampler.prepare(1, 0.99, 1.01));

	const auto result = resampler.process(inputs, inputRate, outputs,
		static_cast<int>(output.size()), factor, true);

	EXPECT_EQ(result.inputSamplesUsed, inputRate);
	EXPECT_NEAR(result.outputSamplesGenerated, outputRate, 3);
}
