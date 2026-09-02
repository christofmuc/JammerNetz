/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PathMtuDiscovery.h"

#include <array>

namespace {

constexpr std::array<int, 6> growthTargets { 1200, 1472, 2048, 4096, 8192, 16384 };

} // namespace

void PathMtuDiscovery::setSupported(bool supported)
{
	if (!supported) {
		status_ = PathMtuDiscoveryStatus::Unavailable;
		awaitingAcknowledgement_ = false;
		return;
	}
	if (status_ != PathMtuDiscoveryStatus::Unavailable) {
		return;
	}

	status_ = PathMtuDiscoveryStatus::Searching;
	safePayloadBytes_ = 0;
	candidatePayloadBytes_ = fallbackPayloadBytes;
	upperBoundPayloadBytes_ = 0;
	attempts_ = 0;
	awaitingAcknowledgement_ = false;
}

void PathMtuDiscovery::markFailed()
{
	status_ = PathMtuDiscoveryStatus::Failed;
	awaitingAcknowledgement_ = false;
}

std::optional<PathMtuProbe> PathMtuDiscovery::poll(TimePoint now)
{
	if (status_ != PathMtuDiscoveryStatus::Searching) {
		return std::nullopt;
	}

	if (awaitingAcknowledgement_) {
		if (now < deadline_) {
			return std::nullopt;
		}
		if (attempts_ >= maximumAttempts) {
			awaitingAcknowledgement_ = false;
			recordFailure();
			if (status_ != PathMtuDiscoveryStatus::Searching) {
				return std::nullopt;
			}
		}
	}

	if (!awaitingAcknowledgement_ && now < earliestNextProbe_) {
		return std::nullopt;
	}

	if (!awaitingAcknowledgement_) {
		activeProbe_ = { nextProbeId_++, candidatePayloadBytes_ };
		attempts_ = 0;
		awaitingAcknowledgement_ = true;
	}

	++attempts_;
	deadline_ = now + probeTimeout;
	return activeProbe_;
}

bool PathMtuDiscovery::acknowledge(std::uint64_t id, int payloadBytes, TimePoint now)
{
	if (status_ != PathMtuDiscoveryStatus::Searching || !awaitingAcknowledgement_
		|| activeProbe_.id != id || activeProbe_.payloadBytes != payloadBytes) {
		return false;
	}

	awaitingAcknowledgement_ = false;
	attempts_ = 0;
	earliestNextProbe_ = now + probeTimeout;
	recordSuccess();
	return true;
}

PathMtuDiscoveryStatus PathMtuDiscovery::status() const noexcept
{
	return status_;
}

int PathMtuDiscovery::safePayloadBytes() const noexcept
{
	return safePayloadBytes_ > 0 ? safePayloadBytes_ : fallbackPayloadBytes;
}

void PathMtuDiscovery::recordSuccess()
{
	safePayloadBytes_ = candidatePayloadBytes_;
	if (upperBoundPayloadBytes_ > 0) {
		selectBinarySearchTarget();
	} else {
		selectNextGrowthTarget();
	}
}

void PathMtuDiscovery::recordFailure()
{
	if (safePayloadBytes_ == 0) {
		status_ = PathMtuDiscoveryStatus::Failed;
		return;
	}

	upperBoundPayloadBytes_ = candidatePayloadBytes_ - probeAlignmentBytes;
	selectBinarySearchTarget();
}

void PathMtuDiscovery::selectNextGrowthTarget()
{
	for (const auto target : growthTargets) {
		if (target > safePayloadBytes_) {
			candidatePayloadBytes_ = target;
			return;
		}
	}
	status_ = PathMtuDiscoveryStatus::Complete;
}

void PathMtuDiscovery::selectBinarySearchTarget()
{
	if (upperBoundPayloadBytes_ < safePayloadBytes_ + probeAlignmentBytes) {
		status_ = PathMtuDiscoveryStatus::Complete;
		return;
	}

	const auto alignedHalfSpan = ((upperBoundPayloadBytes_ - safePayloadBytes_) / 2
		/ probeAlignmentBytes) * probeAlignmentBytes;
	candidatePayloadBytes_ = safePayloadBytes_ + alignedHalfSpan;
	if (candidatePayloadBytes_ <= safePayloadBytes_) {
		candidatePayloadBytes_ = safePayloadBytes_ + probeAlignmentBytes;
	}
}
