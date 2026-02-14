/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "RemoteControlDebugLog.h"

#include <iostream>
#include <mutex>

namespace {
struct RemoteControlLogState {
	std::mutex mutex;
	std::unique_ptr<juce::FileOutputStream> stream;
	juce::String path;
	bool disabled = false;
};

RemoteControlLogState& state()
{
	// Intentionally leaked to avoid shutdown order races when background threads still log during teardown.
	static auto *sharedState = new RemoteControlLogState();
	return *sharedState;
}

juce::String nowWithMillis()
{
	auto now = juce::Time::getCurrentTime();
	return now.formatted("%H:%M:%S") + "." + juce::String(now.getMilliseconds()).paddedLeft('0', 3);
}

void ensureLogStream(RemoteControlLogState &s)
{
	if (s.disabled || s.stream) {
		return;
	}

	// Logging is opt-in via JN_REMOTE_LOG_ENABLE.
	auto enabledByEnv = juce::SystemStats::getEnvironmentVariable("JN_REMOTE_LOG_ENABLE", {});
	if (!(enabledByEnv == "1" || enabledByEnv.equalsIgnoreCase("true"))) {
		s.disabled = true;
		return;
	}

	auto logDir = juce::File::getCurrentWorkingDirectory().getChildFile("logs");
	logDir.createDirectory();

	auto logName = juce::SystemStats::getEnvironmentVariable("JN_REMOTE_LOG_NAME", {});
	if (logName.isEmpty()) {
		logName = "remote-control-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H-%M-%S");
	}

	auto logFile = logDir.getChildFile(logName + ".log");
	auto output = std::make_unique<juce::FileOutputStream>(logFile);
	if (!output->openedOk()) {
		s.disabled = true;
		return;
	}

	s.path = logFile.getFullPathName();
	std::cout << "[RemoteControlDebugLog] writing to " << s.path << std::endl;
	output->writeText("# Remote control diagnostics\n", false, false, "\n");
	output->writeText("# " + juce::Time::getCurrentTime().toString(true, true, true, true) + "\n", false, false, "\n");
	output->flush();
	s.stream = std::move(output);
}
}

namespace RemoteControlDebugLog {
void logEvent(const juce::String &source, const juce::String &message)
{
	auto &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	ensureLogStream(s);
	if (!s.stream) {
		return;
	}

	auto line = nowWithMillis()
		+ " [" + source + "] " + message + "\n";
	s.stream->writeText(line, false, false, "\n");
	s.stream->flush();
}
}
