/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/


#pragma once

#include "JuceHeader.h"

class Tuner {
public:
	Tuner();
	~Tuner();

	void detectPitch(std::shared_ptr<AudioBuffer<float>> audioBuffer);
	float getPitch(size_t channel) const;

private:
	// We might want to try different pitch detection libraries, therefore hide the implementation
	class TunerImpl;
	std::unique_ptr<TunerImpl> impl;
};
