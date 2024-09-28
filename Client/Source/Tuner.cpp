/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Tuner.h"

#include "BuffersConfig.h"

#pragma warning( push )
#pragma warning( disable: 4244 4267 4305 4456)
#include <q/pitch/pitch_detector.hpp>
#pragma warning ( pop )

namespace q = cycfi::q;
using namespace q::literals;

class Tuner::TunerImpl {
public:
	void detectPitch(std::shared_ptr<AudioBuffer<float>> audioData) {
		if (audioData) {

			auto readPointers = audioData->getArrayOfReadPointers();
			for (int channel = 0; channel < audioData->getNumChannels(); channel++) {
				// Do we already create a detector for this channel?
				if (detectors.size() <= channel) {
					detectors.push_back(std::make_unique<q::pitch_detector>(50.0_Hz, 2000.0_Hz, (float) SAMPLE_RATE, q::lin_to_db(0.0)));
				}

				// Feed the samples of this channel into the pitch detector
				for (int s = 0; s < audioData->getNumSamples(); s++) {
					(*detectors[channel])(readPointers[channel][s]);
				}
			}
		}
	}

	float getPitch(int channel) const {
		if (channel < detectors.size()) {
			return detectors[channel]->predict_frequency();
		}
		else {
			return 0.0f;
		}
	}

private:
	std::vector<std::unique_ptr<q::pitch_detector>> detectors;
};

Tuner::Tuner()
{
	impl = std::make_unique<TunerImpl>();
}

Tuner::~Tuner() = default;

void Tuner::detectPitch(std::shared_ptr<AudioBuffer<float>> audioBuffer)
{
	impl->detectPitch(audioBuffer);
}

float Tuner::getPitch(int channel) const
{
	return impl->getPitch(channel);
}
