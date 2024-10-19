/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Tuner.h"

#include "BuffersConfig.h"

#ifndef __GNUC__
#pragma warning( push )
#pragma warning( disable: 4244 4267 4305 4456)
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-float-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <q/pitch/pitch_detector.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifndef __GNUC__
#pragma warning ( pop )
#endif


namespace q = cycfi::q;
using namespace q::literals;

class Tuner::TunerImpl {
public:
	void detectPitch(std::shared_ptr<AudioBuffer<float>> audioData) {
		if (audioData) {

			auto readPointers = audioData->getArrayOfReadPointers();
			for (size_t channel = 0; channel < (size_t) audioData->getNumChannels(); channel++) {
				// Do we already create a detector for this channel?
				if (detectors.size() <= channel) {
					detectors.push_back(std::make_unique<q::pitch_detector>(50.0_Hz, 2000.0_Hz, (float) SAMPLE_RATE, q::lin_to_db(0.0)));
				}

				// Feed the samples of this channel into the pitch detector
				for (size_t s = 0; s < (size_t) audioData->getNumSamples(); s++) {
					(*detectors[channel])(readPointers[channel][s]);
				}
			}
		}
	}

	float getPitch(size_t channel) const {
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

float Tuner::getPitch(size_t channel) const
{
	return impl->getPitch(channel);
}
