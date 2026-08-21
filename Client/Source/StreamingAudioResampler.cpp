/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "StreamingAudioResampler.h"

#include <libresample.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

struct StreamingAudioResampler::Impl {
	int channels { 0 };
	double minimumFactor { 1.0 };
	double maximumFactor { 1.0 };
	bool highQuality { true };
	bool hasResampled { false };
	std::vector<void*> handles;

	~Impl()
	{
		close();
	}

	void close() noexcept
	{
		for (auto* handle : handles) {
			resample_close(handle);
		}
		handles.clear();
	}

	bool open()
	{
		close();
		hasResampled = false;
		handles.reserve(static_cast<std::size_t>(channels));
		for (int channel = 0; channel < channels; ++channel) {
			auto* handle = resample_open(highQuality ? 1 : 0, minimumFactor, maximumFactor);
			if (!handle) {
				close();
				return false;
			}
			handles.push_back(handle);
		}
		return true;
	}
};

StreamingAudioResampler::StreamingAudioResampler() = default;
StreamingAudioResampler::~StreamingAudioResampler() = default;
StreamingAudioResampler::StreamingAudioResampler(StreamingAudioResampler&&) noexcept = default;
StreamingAudioResampler& StreamingAudioResampler::operator=(StreamingAudioResampler&&) noexcept = default;

bool StreamingAudioResampler::prepare(const int channels, const double minimumFactor,
	const double maximumFactor, const bool highQuality)
{
	if (channels <= 0 || !std::isfinite(minimumFactor) || !std::isfinite(maximumFactor)
		|| minimumFactor <= 0.0 || maximumFactor < minimumFactor) {
		return false;
	}
	impl_ = std::make_unique<Impl>();
	impl_->channels = channels;
	impl_->minimumFactor = minimumFactor;
	impl_->maximumFactor = maximumFactor;
	impl_->highQuality = highQuality;
	if (!impl_->open()) {
		impl_.reset();
		return false;
	}
	return true;
}

void StreamingAudioResampler::reset()
{
	if (impl_) {
		impl_->open();
	}
}

StreamingAudioResampler::ProcessResult StreamingAudioResampler::process(
	const float* const* input, const int inputSamples, float* const* output,
	const int outputCapacity, const double factor, const bool endOfInput) noexcept
{
	if (!impl_ || !input || !output || inputSamples < 0 || outputCapacity <= 0
		|| factor < impl_->minimumFactor || factor > impl_->maximumFactor
		|| !std::isfinite(factor)) {
		return {};
	}
	for (int channel = 0; channel < impl_->channels; ++channel) {
		if ((inputSamples > 0 && !input[channel]) || !output[channel]) {
			return {};
		}
	}

	// The exact-unity path is both cheaper and sample-perfect. This leaves
	// correctly clocked 48 kHz devices entirely outside the filter path.
	if (!impl_->hasResampled && std::abs(factor - 1.0) <= 1.0e-12) {
		const auto samples = std::min(inputSamples, outputCapacity);
		for (int channel = 0; channel < impl_->channels; ++channel) {
			std::copy_n(input[channel], samples, output[channel]);
		}
		return { samples, samples };
	}
	impl_->hasResampled = true;

	ProcessResult result;
	for (int channel = 0; channel < impl_->channels; ++channel) {
		int inputUsed = 0;
		const auto generated = resample_process(impl_->handles[static_cast<std::size_t>(channel)],
			factor, const_cast<float*>(input[channel]), inputSamples, endOfInput ? 1 : 0,
			&inputUsed, output[channel], outputCapacity);
		if (generated < 0) {
			return {};
		}
		if (channel == 0) {
			result = { inputUsed, generated };
		}
		else if (result.inputSamplesUsed != inputUsed
			|| result.outputSamplesGenerated != generated) {
			return {};
		}
	}
	return result;
}

int StreamingAudioResampler::filterWidth() const noexcept
{
	return impl_ && !impl_->handles.empty() ? resample_get_filter_width(impl_->handles.front()) : 0;
}
