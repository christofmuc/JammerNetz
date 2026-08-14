/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "BuffersConfig.h"
#include "JuceHeader.h"
#include "JammerNetzPackage.h"

#include <array>
#include <optional>

constexpr int JAMMERNETZ_MAX_AUDIO_CHANNELS = 64;
constexpr int JAMMERNETZ_MAX_CALLBACK_SAMPLES = 8192;

struct TransmitAudioFrame {
	int channels { 0 };
	std::array<std::array<float, SAMPLE_BUFFER_SIZE>, JAMMERNETZ_MAX_AUDIO_CHANNELS> samples {};
	std::optional<float> bpm;
	std::optional<MidiSignal> midiSignal;
};

struct RemoteAudioFrame {
	std::array<std::array<float, SAMPLE_BUFFER_SIZE>, 2> samples {};
	double sourceTimestamp { 0.0 };
	uint64 generation { 0 };
};

enum class RecordingTarget : uint8_t { local, master };

struct RecordingAudioFrame {
	RecordingTarget target { RecordingTarget::local };
	int channels { 0 };
	int samplesPerChannel { 0 };
	std::array<std::array<float, JAMMERNETZ_MAX_CALLBACK_SAMPLES>, JAMMERNETZ_MAX_AUDIO_CHANNELS> samples {};
};
