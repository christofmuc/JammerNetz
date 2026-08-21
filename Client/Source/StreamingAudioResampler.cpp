/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "StreamingAudioResampler.h"

#include <algorithm>
#include <cmath>

struct StreamingAudioResampler::Impl {
	int channels { 0 };
	double minimumFactor { 1.0 };
	double maximumFactor { 1.0 };
};

StreamingAudioResampler::StreamingAudioResampler() = default;
StreamingAudioResampler::~StreamingAudioResampler() = default;
StreamingAudioResampler::StreamingAudioResampler(StreamingAudioResampler&&) noexcept = default;
StreamingAudioResampler& StreamingAudioResampler::operator=(StreamingAudioResampler&&) noexcept = default;

bool StreamingAudioResampler::prepare(const int channels, const double minimumFactor,
	const double maximumFactor, const bool /*highQuality*/)
{
	if (channels <= 0 || !std::isfinite(minimumFactor) || !std::isfinite(maximumFactor)
		|| minimumFactor <= 0.0 || maximumFactor < minimumFactor) {
		return false;
	}
	impl_ = std::make_unique<Impl>();
	impl_->channels = channels;
	impl_->minimumFactor = minimumFactor;
	impl_->maximumFactor = maximumFactor;
	return true;
}

void StreamingAudioResampler::reset()
{
}

StreamingAudioResampler::ProcessResult StreamingAudioResampler::process(
	const float* const* input, const int inputSamples, float* const* output,
	const int outputCapacity, const double factor, const bool /*endOfInput*/) noexcept
{
	if (!impl_ || !input || !output || inputSamples <= 0 || outputCapacity <= 0
		|| factor < impl_->minimumFactor || factor > impl_->maximumFactor
		|| std::abs(factor - 1.0) > 1.0e-12) {
		return {};
	}
	const auto samples = std::min(inputSamples, outputCapacity);
	for (int channel = 0; channel < impl_->channels; ++channel) {
		if (!input[channel] || !output[channel]) {
			return {};
		}
		std::copy_n(input[channel], samples, output[channel]);
	}
	return { samples, samples };
}

int StreamingAudioResampler::filterWidth() const noexcept
{
	return 0;
}
