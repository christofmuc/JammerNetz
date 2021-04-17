/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PacketStreamQueue.h"

PacketStreamQueue::PacketStreamQueue(std::string const &streamName) : lastPoppedMessage_(0), lastPushedMessage_(0), currentGap_(0)
{
	qualityData_.streamName = streamName;
}

bool PacketStreamQueue::push(std::shared_ptr<JammerNetzAudioData> packet)
{
	if (!hasBeenPushedBefore(packet)) {
		currentlyInQueue_.insert(std::make_pair(packet->messageCounter(), true));
		qualityData_.packagesPushed++;
		packetQueue.push(packet);
		if (packet->messageCounter() < lastPushedMessage_) {
			// Ups, this came in out of order (but not too late, else we classify it as "tooLateOrDuplicate")
			qualityData_.outOfOrderPacketCounter++;
			qualityData_.maxWrongOrderSpan = std::max((unsigned long long) qualityData_.maxWrongOrderSpan, lastPushedMessage_ - packet->messageCounter());
		}
		lastPushedMessage_ = packet->messageCounter();

		// Calculate the jitter in this queue!
		double now = Time::getMillisecondCounterHiRes();

		// Jitter is the difference between the running mean of clock delta and this packages clock delta
		double clockDelta = now - packet->timestamp();
		if (runningMeanClockDelta_.NumDataValues() > 0) {
			double jitter = fabs(clockDelta - runningMeanClockDelta_.Mean());
			runningMeanJitter_.Push(jitter);
			qualityData_.jitterMeanMillis = runningMeanJitter_.Mean();
			qualityData_.jitterSDMillis = runningMeanJitter_.StandardDeviation();
		}
		runningMeanClockDelta_.Push(clockDelta);

		// Every 30 seconds forget the rolling mean. Well, at 48000 KHz and 128 buffer size
		if (runningMeanClockDelta_.NumDataValues() > 1875 * 6) {
			runningMeanClockDelta_.Clear();
			runningMeanJitter_.Clear();
		}
		return true;
	}
	return false;
}

bool PacketStreamQueue::try_pop(std::shared_ptr<JammerNetzAudioData> &element, bool &outIsFillIn)
{
	std::shared_ptr<JammerNetzAudioData> packet;
	if (!packetQueue.try_pop(packet)) {
		return false;
	}

	// Is this the correct package?
	if ((lastPoppedMessage_ + 1 == packet->messageCounter()) || !lastPoppedMessageData_) {
#ifdef FAKE_DROPS
		if (rand() % 10 == 0) {
			fakeDroppedMessage_ = packet;
			return false;
		}
#endif
		// This is either the very first message, or:
		// Great, no gap, and the correct data has been retrieved. Happy to continue!
		lastPoppedMessage_ = packet->messageCounter();
		lastPoppedMessageData_ = packet;
		currentlyInQueue_.erase(lastPoppedMessage_);
		element = packet;
		currentGap_ = 0;
		outIsFillIn = false;
		qualityData_.packagesPopped++;
		return true;
	} 
	else {
		// Ok, as we are at the bottom of the buffer, we give up hope that the packet we were looking for still arrives
		// Consider it MIA and use the one we popped to create a fill in package, maybe FEC can help. And it needs to go back into the priority queue
		packetQueue.push(packet);
		if (currentGap_ < 1) {
			bool hadFEC;
			element = packet->createFillInPackage(lastPoppedMessage_ + 1, hadFEC);
			if (hadFEC) {
				qualityData_.dropsHealed++;
			}
			else {
				qualityData_.droppedPacketCounter++;
			}
		}
		else {
			// Never repeat this again, take the next package even if there was a drop and restart consecutive counting
			bool hadFEC;
			element = packet->createFillInPackage(packet->messageCounter() - 1, hadFEC);
			element->audioBuffer()->clear();
			qualityData_.droppedPacketCounter++;
		}
		lastPoppedMessage_ = element->messageCounter();
		currentGap_++;
		qualityData_.maxLengthOfGap = std::max((uint64)qualityData_.maxLengthOfGap, (uint64)currentGap_);
		outIsFillIn = true;
		return true;
	}
}

size_t PacketStreamQueue::size() const
{
	return packetQueue.size();
}

std::string PacketStreamQueue::qualityStatement() const
{
	return qualityData_.qualityStatement();
}

JammerNetzStreamQualityInfo PacketStreamQueue::qualityInfoPackage() const
{
	return qualityData_.qualityInfoPackage();
}

bool PacketStreamQueue::hasBeenPushedBefore(std::shared_ptr<JammerNetzAudioData> packet)
{
	// Easy case - if the message number of the packet is lower than the number of the last popped packet, it is too old
	// Either it came out of order too late, or it is a duplicate!
	if (packet->messageCounter() < lastPoppedMessage_) {
		qualityData_.tooLateOrDuplicate++;
		return true;
	}
	// Else we rely on the set<> that tracks the messages we have pushed into the queue but not popped
	tbb::concurrent_hash_map<uint64, bool>::const_accessor found_accessor;
	if (currentlyInQueue_.find(found_accessor, packet->messageCounter())) {
		qualityData_.duplicatePacketCounter++;
		return true;
	}
	return false;
}

StreamQualityData::StreamQualityData()
{
	tooLateOrDuplicate = 0;
	droppedPacketCounter = 0;
	outOfOrderPacketCounter = 0;
	duplicatePacketCounter = 0;
	dropsHealed = 0;
	packagesPushed = 0;
	packagesPopped = 0;
	maxLengthOfGap = 0;
	maxWrongOrderSpan = 0;
	jitterMeanMillis = 0.0;
	jitterSDMillis = 0.0;
}

std::string StreamQualityData::qualityStatement() const {
	std::stringstream text;
	text << streamName << " quality: "
		//<< packagesPushed << " push, "
		//<< packagesPopped << " pop, "
		<< packagesPushed - packagesPopped << " len, "
		<< outOfOrderPacketCounter << " ooO, "
		<< maxWrongOrderSpan << " span, "
		<< duplicatePacketCounter << " dup, "
		<< dropsHealed << " heal, "
		<< tooLateOrDuplicate << " late, "
		<< droppedPacketCounter << " drop ("
		<< std::setprecision(2) << droppedPacketCounter / (float)packagesPopped * 100.0f <<"%), "
		<< maxLengthOfGap << " gap";
		
	return text.str();
}

JammerNetzStreamQualityInfo StreamQualityData::qualityInfoPackage() const
{
	JammerNetzStreamQualityInfo result;
	result.tooLateOrDuplicate = tooLateOrDuplicate;
	result.droppedPacketCounter = droppedPacketCounter;
	result.outOfOrderPacketCounter = outOfOrderPacketCounter;
	result.duplicatePacketCounter = duplicatePacketCounter;
	result.dropsHealed = dropsHealed;
	result.packagesPushed = packagesPushed;
	result.packagesPopped = packagesPopped;
	result.maxLengthOfGap = maxLengthOfGap;
	result.maxWrongOrderSpan = maxWrongOrderSpan;
	result.jitterMeanMillis = jitterMeanMillis;
	result.jitterSDMillis = jitterSDMillis;
	return result;
}
