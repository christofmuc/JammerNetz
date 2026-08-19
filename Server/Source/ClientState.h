/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "PacketStreamQueue.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

enum class ClientConnectionState {
	Disconnected,
	Connected,
	Disconnecting
};

enum class ClientConnectionTransition {
	None,
	InitialConnection,
	GraceRecovery,
	Reconnection
};

struct ClientPushResult {
	bool queued;
	ClientConnectionTransition transition;
};

struct ClientQueueSnapshot {
	ClientConnectionState state;
	std::size_t size;
	std::uint64_t activityGeneration;
};

struct ClientQueuePressureResult {
	ClientQueueSnapshot before;
	ClientQueueSnapshot after;
	PacketStreamQueueFastForwardResult fastForward;
};

// Owns one client's queue and connection state. Queue ownership never escapes this
// class, so disconnect/reconnect cannot invalidate another thread's queue access.
class ClientState {
public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	static constexpr auto DisconnectGracePeriod = std::chrono::seconds(2);

	explicit ClientState(std::string clientName);

	ClientPushResult push(std::shared_ptr<JammerNetzAudioData> packet,
		std::size_t initialPrefillCount, TimePoint now = Clock::now());
	bool tryPop(std::shared_ptr<JammerNetzAudioData> &packet, bool &isFillIn,
		std::uint64_t &observedActivityGeneration);
	ClientQueuePressureResult applyQueuePressure(std::size_t maximumPacketCount,
		std::size_t retainedPacketCount);
	ClientQueueSnapshot snapshot() const;
	bool qualityInfo(JammerNetzStreamQualityInfo &qualityInfo) const;

	bool markUnderrun(std::uint64_t observedActivityGeneration, TimePoint now = Clock::now());
	bool disconnectIfGraceExpired(TimePoint now = Clock::now());

private:
	mutable std::mutex mutex_;
	std::string clientName_;
	ClientConnectionState state_{ClientConnectionState::Disconnected};
	std::shared_ptr<PacketStreamQueue> queue_;
	TimePoint disconnectDeadline_{};
	std::uint64_t activityGeneration_{0};
	bool hasConnected_{false};
};
