/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JammerNetzPackage.h"

#include <memory>
#include <optional>

struct ControlData
{
	std::optional<float> bpm;
	std::optional<MidiSignal> midiSignal;
};

// Boundary between audio processing and packet construction/transport. The audio
// buffer is valid for the duration of sendData; implementations that retain it must copy it.
class AudioPacketSink {
public:
	virtual ~AudioPacketSink() = default;

	virtual bool sendData(JammerNetzChannelSetup const& channelSetup,
		std::shared_ptr<AudioBuffer<float>> audioBuffer,
		ControlData controllers) = 0;
};
