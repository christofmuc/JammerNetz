/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "JammerNetzPackage.h"
#include "BuffersConfig.h"

#include "Recorder.h"
#include "ServerMixScheduler.h"

class MixerThread : public Thread {
public:
	MixerThread(TPacketStreamBundle &incoming, JammerNetzChannelSetup mixdownSetup, TOutgoingQueue &outgoing, TMessageQueue &wakeUpQueue
                /*, Recorder &recorder*/
                , ServerBufferConfig bufferConfig);

	virtual void run() override;

private:
	TPacketStreamBundle &incoming_;
	TOutgoingQueue &outgoing_;
	TMessageQueue &wakeUpQueue_;
	ServerMixScheduler mixScheduler_;
	//Recorder &recorder_;
};
