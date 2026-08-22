/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <cstddef>
#include <memory>

class StreamingAudioResampler {
public:
	struct ProcessResult {
		int inputSamplesUsed { 0 };
		int outputSamplesGenerated { 0 };
	};

	StreamingAudioResampler();
	~StreamingAudioResampler();
	StreamingAudioResampler(StreamingAudioResampler&&) noexcept;
	StreamingAudioResampler& operator=(StreamingAudioResampler&&) noexcept;

	StreamingAudioResampler(const StreamingAudioResampler&) = delete;
	StreamingAudioResampler& operator=(const StreamingAudioResampler&) = delete;

	bool prepare(int channels, double minimumFactor, double maximumFactor,
		bool highQuality = true);
	bool reset() noexcept;
	ProcessResult process(const float* const* input, int inputSamples,
		float* const* output, int outputCapacity, double factor,
		bool endOfInput = false) noexcept;
	int filterWidth() const noexcept;
	bool isPrepared() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
