/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "JammerNetzClientInfoMessage.h"

#include "RunningStats.h"

#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

struct StreamQualityData {
	StreamQualityData();

	// Unhealed problems
	std::atomic_uint64_t tooLateOrDuplicate;
	std::atomic_int64_t droppedPacketCounter;

	// Healed problems
	std::atomic_int64_t outOfOrderPacketCounter;
	std::atomic_int64_t duplicatePacketCounter;
	std::atomic_uint64_t dropsHealed;

	// Pure statistics
	std::atomic_uint64_t packagesPushed;
	std::atomic_uint64_t packagesPopped;
	std::atomic_uint64_t maxLengthOfGap;
	std::atomic_uint64_t maxWrongOrderSpan;

	// Measure jitter in queue
	std::atomic<double> jitterMeanMillis;
	std::atomic<double> jitterSDMillis;

	std::string streamName;

	std::string qualityStatement() const;
	JammerNetzStreamQualityInfo qualityInfoPackage() const;

};

struct PacketStreamQueueFastForwardResult {
	std::size_t discardedPackets { 0 };
	std::optional<std::uint64_t> oldestRetainedCounter;
};

class PacketStreamQueue {
public:
	PacketStreamQueue(std::string const &streamName);

	bool push(std::shared_ptr<JammerNetzAudioData> packet);
	bool try_pop(std::shared_ptr<JammerNetzAudioData> &element, bool &outIsFillIn);
	PacketStreamQueueFastForwardResult fastForwardToSize(std::size_t retainedPacketCount);
	void reset();
	size_t size() const;

	std::string qualityStatement() const;
	JammerNetzStreamQualityInfo qualityInfoPackage() const;

private:
	bool hasBeenPushedBefore(std::shared_ptr<JammerNetzAudioData> packet);

	std::priority_queue<
		std::shared_ptr<JammerNetzAudioData>,
		std::vector<std::shared_ptr<JammerNetzAudioData>>,
		JammerNetzAudioOrder> packetQueue_;
	std::atomic_uint64_t lastPushedMessage_;
	std::atomic_uint64_t lastPoppedMessage_;
#ifdef FAKE_DROPS
	std::shared_ptr<JammerNetzAudioData>  fakeDroppedMessage_;
#endif
	std::atomic_uint64_t currentGap_;
	std::shared_ptr<JammerNetzAudioData> lastPoppedMessageData_;
	RunningStats runningMeanClockDelta_;
	RunningStats runningMeanJitter_;
	StreamQualityData qualityData_;
	std::unordered_set<std::uint64_t> currentlyInQueue_;
};
