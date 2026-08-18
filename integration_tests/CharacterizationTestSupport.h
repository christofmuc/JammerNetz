/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "ClientState.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace jammernetz::test {

inline const char* connectionStateName(const ClientConnectionState state)
{
	switch (state) {
	case ClientConnectionState::Disconnected: return "disconnected";
	case ClientConnectionState::Connected: return "connected";
	case ClientConnectionState::Disconnecting: return "disconnecting";
	}
	return "unknown";
}

inline void writeJsonArtifact(const juce::File& path, const nlohmann::json& document,
	const std::string_view description)
{
	const auto parent = path.getParentDirectory();
	if (!parent.exists() && parent.createDirectory().failed()) {
		throw std::runtime_error("Could not create " + std::string(description) + " artifact directory");
	}
	auto output = path.createOutputStream();
	if (!output) {
		throw std::runtime_error("Could not create " + std::string(description) + " artifact");
	}
	if (!output->setPosition(0)) {
		throw std::runtime_error("Could not seek to the start of " + std::string(description) + " artifact");
	}
	if (output->truncate().failed()) {
		throw std::runtime_error("Could not truncate " + std::string(description) + " artifact");
	}
	const auto text = document.dump(2);
	if (!output->write(text.data(), text.size())) {
		throw std::runtime_error("Could not write " + std::string(description) + " artifact");
	}
	output->flush();
}

} // namespace jammernetz::test
