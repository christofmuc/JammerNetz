/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

constexpr const char* VALUE_USER_NAME = "userName";
constexpr const char* VALUE_INPUT_LATENCY = "inputLatency";
constexpr const char* VALUE_OUTPUT_LATENCY = "outputLatency";
constexpr const char* VALUE_INPUT_SETUP = "Inputs";
constexpr const char* VALUE_OUTPUT_SETUP = "Outputs";
constexpr const char* VALUE_MIXER = "Mixer";
constexpr const char* VALUE_VOLUME = "Volume";
constexpr const char* VALUE_TARGET = "Target";
constexpr const char* VALUE_MIN_PLAYOUT_BUFFER = "minPlayoutBuffer";
constexpr const char* VALUE_MAX_PLAYOUT_BUFFER = "maxPlayoutBuffer";
constexpr const char* VALUE_USE_FEC = "useFEC";
constexpr const char* VALUE_SERVER_NAME = "ServerName";
constexpr const char* VALUE_SERVER_PORT = "Port";
constexpr const char* VALUE_USE_LOCALHOST = "UseLocalhost";
constexpr const char* VALUE_CRYPTOPATH = "CryptoFilePath";
constexpr const char* VALUE_DEVICE_TYPE = "Type";
constexpr const char* VALUE_DEVICE_NAME = "Device";
constexpr const char* VALUE_CHANNELS = "Channels";
constexpr const char* VALUE_CHANNEL_COUNT = "ChannelCount";
constexpr const char* VALUE_CHANNEL_ACTIVE = "ChannelActive";
constexpr const char* VALUE_CHANNEL_NAME = "ChannelName";

constexpr const char* EPHEMERAL_VALUE_DEVICE_TYPES_AVAILABLE = "AudioDeviceTypes";

class ValueListener : public juce::Value::Listener {
public:
	ValueListener(Value value, std::function<void(Value& newValue)> onChanged);

	virtual ~ValueListener();

	void valueChanged(Value& value) override;
	void triggerOnChanged();

private:
	Value value_;
	std::function<void(Value& newValue)> onChanged_;
};

class ValueTreeUtils {
public:
	static bool isChildOf(Identifier searchFor, ValueTree child);
};