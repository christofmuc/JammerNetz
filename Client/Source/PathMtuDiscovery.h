/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

struct PathMtuProbe {
	std::uint64_t id;
	int payloadBytes;
};

enum class PathMtuDiscoveryStatus {
	Unavailable,
	Searching,
	Complete,
	Failed
};

class PathMtuDiscovery {
public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	static constexpr int fallbackPayloadBytes = 1200;
	static constexpr int maximumProbePayloadBytes = 16384;
	static constexpr int probeAlignmentBytes = 8;
	static constexpr int maximumAttempts = 3;
	static constexpr auto probeTimeout = std::chrono::milliseconds(500);

	void setSupported(bool supported);
	void markFailed();
	std::optional<PathMtuProbe> poll(TimePoint now = Clock::now());
	bool acknowledge(std::uint64_t id, int payloadBytes, TimePoint now = Clock::now());

	[[nodiscard]] PathMtuDiscoveryStatus status() const noexcept;
	[[nodiscard]] int safePayloadBytes() const noexcept;

private:
	void recordSuccess();
	void recordFailure();
	void selectNextGrowthTarget();
	void selectBinarySearchTarget();

	PathMtuDiscoveryStatus status_ { PathMtuDiscoveryStatus::Unavailable };
	int safePayloadBytes_ { 0 };
	int candidatePayloadBytes_ { fallbackPayloadBytes };
	int upperBoundPayloadBytes_ { 0 };
	std::uint64_t nextProbeId_ { 1 };
	PathMtuProbe activeProbe_ {};
	int attempts_ { 0 };
	bool awaitingAcknowledgement_ { false };
	TimePoint deadline_ {};
	TimePoint earliestNextProbe_ {};
};
