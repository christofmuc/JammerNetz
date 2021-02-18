/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "SharedServerTypes.h"
#include "JammerNetzPackage.h"

#include "Recorder.h"

class MixerThread : public Thread {
public:
	MixerThread(TPacketStreamBundle &incoming, JammerNetzChannelSetup mixdownSetup, TOutgoingQueue &outgoing, TMessageQueue &wakeUpQueue, Recorder &recorder);

	virtual void run() override;

private:
	void bufferMixdown(std::shared_ptr<AudioBuffer<float>> &outBuffer, std::shared_ptr<JammerNetzAudioData> const &audioData, bool isForSender);

	TPacketStreamBundle &incoming_;
	TOutgoingQueue &outgoing_;
	TMessageQueue &wakeUpQueue_;
	JammerNetzChannelSetup mixdownSetup_;
	Recorder &recorder_;
};
