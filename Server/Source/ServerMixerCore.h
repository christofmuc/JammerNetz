/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "SharedServerTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using ServerInputPackets = std::map<std::string, std::shared_ptr<JammerNetzAudioData>>;
using ServerMixRecipients = std::map<std::string, ClientMixMetadata>;

struct ServerMixStepResult {
	uint64 serverTime { 0 };
	uint64 mixSequence { 0 };
	std::vector<OutgoingPackage> outgoing;
	std::vector<std::string> diagnostics;
};

// Stateful, synchronous part of the server mixer. Queue readiness and connection
// transitions remain owned by MixerThread; this class has no threads or sockets.
class ServerMixerCore {
public:
	explicit ServerMixerCore(JammerNetzChannelSetup mixdownSetup);

	ServerMixStepResult mix(const ServerInputPackets& incoming);
	ServerMixStepResult mix(const ServerInputPackets& incoming,
		const ServerMixRecipients& recipients);

private:
	static void bufferMixdown(AudioBuffer<float>& output,
		const JammerNetzAudioData& audioData,
		bool isForSender,
		std::vector<std::string>& diagnostics);

	uint64 serverTime_ { 0 };
	uint64 mixSequence_ { 0 };
	float lastBpm_ { 120.0f };
	JammerNetzChannelSetup mixdownSetup_;
};
