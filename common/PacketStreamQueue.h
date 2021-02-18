/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "JammerNetzPackage.h"
#include "JammerNetzClientInfoMessage.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_priority_queue.h"


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

	std::string streamName;

	std::string qualityStatement() const;
	JammerNetzStreamQualityInfo qualityInfoPackage() const;
};

class PacketStreamQueue {
public:
	PacketStreamQueue(std::string const &streamName);

	bool push(std::shared_ptr<JammerNetzAudioData> packet);
	bool try_pop(std::shared_ptr<JammerNetzAudioData> &element, bool &outIsFillIn);
	size_t size() const;

	std::string qualityStatement() const;
	JammerNetzStreamQualityInfo qualityInfoPackage() const;

private:
	bool hasBeenPushedBefore(std::shared_ptr<JammerNetzAudioData> packet);

	tbb::concurrent_priority_queue<std::shared_ptr<JammerNetzAudioData>, JammerNetzAudioOrder> packetQueue;
	std::atomic_uint64_t lastPushedMessage_;
	std::atomic_uint64_t lastPoppedMessage_;
#ifdef FAKE_DROPS
	std::shared_ptr<JammerNetzAudioData>  fakeDroppedMessage_;
#endif
	std::atomic_uint64_t currentGap_;
	std::shared_ptr<JammerNetzAudioData> lastPoppedMessageData_;
	StreamQualityData qualityData_;
	tbb::concurrent_hash_map<uint64, bool> currentlyInQueue_;
};
