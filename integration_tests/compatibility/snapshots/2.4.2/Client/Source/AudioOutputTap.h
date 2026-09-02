/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

// Optional observer for the final audio-engine output. Implementations must
// keep enqueue() bounded, non-blocking, and allocation-free because it is
// called from the real-time audio callback.
class AudioOutputTap {
public:
	virtual ~AudioOutputTap() = default;

	virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
	virtual void release() = 0;
	virtual bool enqueue(const float* const* channels, int numChannels, int numSamples) noexcept = 0;
};
