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
        , mixScheduler_(std::move(mixdownSetup), bufferConfig)
        /*, recorder_(recorder) */
{
}

void MixerThread::run() {
	while (!currentThreadShouldExit()) {
		// Wait for the accept thread to signal a new package, and then see if we have work to do
		// As this is a bounded queue, the pop() will block
		int message;
		wakeUpQueue_.pop(message);

		auto result = mixScheduler_.process(incoming_);
		if (result.shouldWakeAgain) {
			wakeUpQueue_.push(0);
		}
		for (const auto& client : result.disconnectedClients) {
			ServerLogger::printClientStatus(4, client, "Disconnect grace period expired");
		}
		for (const auto& client : result.underrunClients) {
			ServerLogger::printClientStatus(4, client,
				"Jitter queue underrun, starting disconnect grace period");
		}
		if (!result.incoming.empty()) {
			for (const auto& diagnostic : result.mix.diagnostics) {
				ServerLogger::errorln(diagnostic);
			}
			for (const auto& package : result.mix.outgoing) {
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
