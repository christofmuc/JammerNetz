/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

constexpr const char* VALUE_SERVER_NAME = "serverName";
constexpr const char* VALUE_USER_NAME = "userName";
constexpr const char* VALUE_INPUT_LATENCY = "inputLatency";
constexpr const char* VALUE_OUTPUT_LATENCY = "outputLatency";

class ValueListener : public juce::Value::Listener {
public:
	ValueListener(Value value, std::function<void(Value& newValue)> onChanged) : value_(value), onChanged_(onChanged) {
		value_.addListener(this);
	}

	virtual ~ValueListener() {
		value_.removeListener(this);
	}

	;

	void valueChanged(Value& value) override {
		if (onChanged_) {
			onChanged_(value);
		}
	}

private:
	Value value_;
	std::function<void(Value& newValue)> onChanged_;
};

