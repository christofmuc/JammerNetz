#pragma once

#include "JuceHeader.h"

constexpr const char* VALUE_SERVER_NAME = "serverName";
constexpr const char* VALUE_USER_NAME = "userName";

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

class ApplicationState {
public:
	static ValueTree sApplicationState;

	static void fromSettings();
	static void toSettings();
};

