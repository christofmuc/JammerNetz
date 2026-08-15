/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MixerThread.h"

#include "BuffersConfig.h"
#include "ServerLogger.h"

#include <utility>

MixerThread::MixerThread(TPacketStreamBundle &incoming, JammerNetzChannelSetup mixdownSetup, TOutgoingQueue &outgoing, TMessageQueue &wakeUpQueue/*, Recorder &recorder*/, ServerBufferConfig bufferConfig) :
    Thread("MixerThread")
        , incoming_(incoming)
        , outgoing_(outgoing)
        , wakeUpQueue_(wakeUpQueue)
        , mixerCore_(std::move(mixdownSetup))
        /*, recorder_(recorder) */
        , bufferConfig_(bufferConfig)
{
}

void MixerThread::run() {
	while (!currentThreadShouldExit()) {
		// Wait for the accept thread to signal a new package, and then see if we have work to do
		// As this is a bounded queue, the pop() will block
		int message;
		wakeUpQueue_.pop(message);

		// As we are the only one popping from these queues, we can inspect their length and only continue
		// if a) all queues have at least one item to pop or
		// if b) one of the queues exceeds a maximum length - then obviously that client has delivered, but some other client hasn't
		bool allHaveDelivered = false;
		bool queueOverrun = false;
		int clientCount = 0;
		int available = 0;
		for (auto &inqueue : incoming_) {
			if (inqueue.second) {
				if (inqueue.second->disconnectIfGraceExpired()) {
					ServerLogger::printClientStatus(4, inqueue.first, "Disconnect grace period expired");
				}
				const auto snapshot = inqueue.second->snapshot();
				if (snapshot.state == ClientConnectionState::Disconnected) {
					continue;
				}
				clientCount++;
				if (static_cast<int>(snapshot.size) > bufferConfig_.serverIncomingJitterBuffer) available++;
				if (static_cast<int>(snapshot.size) > bufferConfig_.serverIncomingMaximumBuffer) queueOverrun = true; // This is one client much faster than the others
			}
		}
		if (clientCount == available) allHaveDelivered = true;
		if (queueOverrun) wakeUpQueue_.push(0);
		if (!allHaveDelivered && !queueOverrun && clientCount > 1) continue;

		// Ok, we are committed now to mix the data!
		std::map<std::string, std::shared_ptr<JammerNetzAudioData>> incomingData;
		std::map<std::string, std::uint64_t> observedActivity;
		// Try to pop a package from each client
		for (auto &inqueue : incoming_) {
			if (inqueue.second) {
				if (incomingData.find(inqueue.first) == incomingData.end()) {
					// That client hasn't popped yet - try it!
					std::shared_ptr<JammerNetzAudioData> popped;
					bool isFillIn = false;
					std::uint64_t activityGeneration = 0;
					if (inqueue.second->tryPop(popped, isFillIn, activityGeneration)) {
						incomingData[inqueue.first] = popped;
						if (isFillIn) {
							wakeUpQueue_.push(0);
						}
					}
					else if (inqueue.second->snapshot().state != ClientConnectionState::Disconnected) {
						observedActivity[inqueue.first] = activityGeneration;
					}
				}
			}
		}

		// Start a grace period for clients that have not delivered. The activity
		// generation prevents a packet racing this decision from being ignored.
		for (auto inClient = incoming_.cbegin(); inClient != incoming_.cend(); inClient++) {
			if (inClient->second) {
				auto observation = observedActivity.find(inClient->first);
				if (observation != observedActivity.end()) {
					if (inClient->second->markUnderrun(observation->second)) {
						ServerLogger::printClientStatus(4, inClient->first,
							"Jitter queue underrun, starting disconnect grace period");
					}
				}
			}
		}

		// All clients have delivered (or one has a timeout), mix them all together!
		if (!incomingData.empty()) {
			auto result = mixerCore_.mix(incomingData);
			for (const auto& diagnostic : result.diagnostics) {
				ServerLogger::errorln(diagnostic);
			}
			for (const auto& package : result.outgoing) {
				if (!outgoing_.try_push(package)) {
					std::cerr << "send queue length overflow at " << outgoing_.size() << " packets - network down? FATAL!" << std::endl;
					exit(-1);
				}
			}
		}
	}

	// Give the send thread one package to realize it should stop too
	outgoing_.try_push(OutgoingPackage());
}
