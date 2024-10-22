/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MixerThread.h"

#include "BuffersConfig.h"
#include "ServerLogger.h"

MixerThread::MixerThread(TPacketStreamBundle &incoming, JammerNetzChannelSetup mixdownSetup, TOutgoingQueue &outgoing, TMessageQueue &wakeUpQueue/*, Recorder &recorder*/, ServerBufferConfig bufferConfig) :
    Thread("MixerThread")
        , serverTime_(0)
        , lastBpm_(120.0f)
        , incoming_(incoming)
        , outgoing_(outgoing)
        , wakeUpQueue_(wakeUpQueue)
        , mixdownSetup_(mixdownSetup)
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
				clientCount++;
				if ((int)inqueue.second->size() > bufferConfig_.serverIncomingJitterBuffer) available++;
				if ((int)inqueue.second->size() > bufferConfig_.serverIncomingMaximumBuffer) queueOverrun = true; // This is one client much faster than the others
			}
		}
		if (clientCount == available) allHaveDelivered = true;
		if (queueOverrun) wakeUpQueue_.push(0);
		if (!allHaveDelivered && !queueOverrun && clientCount > 1) continue;

		// Ok, we are committed now to mix the data!
		std::map<std::string, std::shared_ptr<JammerNetzAudioData>> incomingData;
		// Try to pop a package from each client
		for (auto &inqueue : incoming_) {
			if (inqueue.second) { // Skip client entries without a queue
				if (incomingData.find(inqueue.first) == incomingData.end()) {
					// That client hasn't popped yet - try it!
					std::shared_ptr<JammerNetzAudioData> popped;
					bool isFillIn;
					if (inqueue.second->try_pop(popped, isFillIn)) {
						incomingData[inqueue.first] = popped;
						if (isFillIn) {
							wakeUpQueue_.push(0);
						}
					}
				}
			}
		}

		// All clients who have not delivered by now are to be gone!
		// While doing this, sort all session infos into a multimap with sender -> session info
		std::multimap<std::string, JammerNetzChannelSetup> allSessionChannels;
		std::vector<std::string> toBeRemoved;
		for (auto inClient = incoming_.cbegin(); inClient != incoming_.cend(); inClient++) {
			if (inClient->second) {
				if (incomingData.find(inClient->first) == incomingData.end()) {
					ServerLogger::printClientStatus(4, inClient->first, "Jitter queue underrun, removing from client list in mix!");
					toBeRemoved.push_back(inClient->first);
				}
				else {
					// TODO- do we need to do this every packet?
					// Is part of mix, list in session info
					allSessionChannels.emplace(inClient->first, incomingData[inClient->first]->channelSetup());
				}
			}
		}
		for (auto remove : toBeRemoved) {
			// Kill the queue of that client
			//TODO - this is not thread safe. I am not allowed to remove the queue, I can only empty it and somewhere else flag the client as connected or not.
			incoming_[remove].reset();
		}

		// All clients have delivered (or one has a timeout), mix them all together!
		if (incomingData.size() > 0) {
			//TODO - current assumption: all clients provide buffers of the same size. Therefore, take the length of the first client as the output size
			int bufferLength = (*incomingData.begin()).second->audioBuffer()->getNumSamples();
			serverTime_ += (juce::uint64) bufferLength; // Server time counts time of mixing thread in samples mixed since launch

			// For each client that has delivered data, produce a mix down package and send it back
			//TODO - also the clients that have not provided data should get a package with a note that they are not contained within - they could do a local fill in.
			for (auto &receiver : incomingData) {
				std::shared_ptr<AudioBuffer<float>> outBuffer = std::make_shared<AudioBuffer<float>>(2, bufferLength);
				outBuffer->clear();

				// We now produce one mix for each client, specific, because you might not want to hear your own voice microphone
				JammerNetzChannelSetup sessionSetup(false);
				float maxBpmSet = 0.0f;
				MidiSignal midiSignal = MidiSignal_None;
				for (auto &client : incomingData) {
					//recorder_.saveBlock(client.second->audioBuffer()->getArrayOfReadPointers(), outBuffer->getNumSamples());
					bufferMixdown(outBuffer, client.second, client.first == receiver.first);
					// Build the client specific session data structure - this is basically all channels except your own
					if (client.first != receiver.first) {
						auto range = allSessionChannels.equal_range(client.first);
						for_each(range.first, range.second, [&](std::pair<const std::string, JammerNetzChannelSetup> setup) {
							std::copy(setup.second.channels.cbegin(), setup.second.channels.cend(), std::back_inserter(sessionSetup.channels));
						});
					}
					// Check if any client requests a new bpm (larger than 0.0 value). Check if a start or stop signal should be sent
					maxBpmSet = std::max(maxBpmSet, client.second->bpm());
					if (client.second->midiSignal() == MidiSignal_Start && midiSignal == MidiSignal_None) {
						midiSignal = MidiSignal_Start;
					}
					if (client.second->midiSignal() == MidiSignal_Stop) {
						midiSignal = MidiSignal_Stop;
					}
				}
				if (maxBpmSet > 0.0f) {
					// This is new information, let's use this
					lastBpm_ = maxBpmSet;
				}

				// The outgoing queue takes packages for all clients, they will be sent out to different addresses
				OutgoingPackage package(receiver.first, AudioBlock(
					receiver.second->timestamp(),
					receiver.second->messageCounter(),
					serverTime_,
					lastBpm_,
					midiSignal,
					48000,
					mixdownSetup_,
					outBuffer),
                    sessionSetup
					);
				if (!outgoing_.try_push(package)) {
					// That's a bad sign - I would assume the sender thread died and that's possibly because the network is down.
					// Abort
					std::cerr << "send queue length overflow at " << outgoing_.size() << " packets - network down? FATAL!" << std::endl;
					exit(-1);
				}
				/*		// Write this to the recorder
						if (!clientRecorder_.isRecording()) {
							//TODO - this needs to compare if it has changed / is different from the one currently being written
							clientRecorder_.setChannelInfo(48000, audioData->channelSetup());
							clientRecorder_.setRecording(true);
						}
						clientRecorder_.saveBlock(audioData->audioBuffer_->getArrayOfReadPointers(), audioData->audioBuffer_->getNumSamples());*/
			}
		}
	}

	// Give the send thread one package to realize it should stop too
	outgoing_.try_push(OutgoingPackage());
}

void MixerThread::bufferMixdown(std::shared_ptr<AudioBuffer<float>> &outBuffer, std::shared_ptr<JammerNetzAudioData> const &audioData, bool isForSender) {
	if (audioData->audioBuffer()->getNumChannels() == 0) {
		ServerLogger::errorln("Got audio block with no channels, somebody needs to setup his interface");
	}
	if (audioData->audioBuffer()->getNumSamples() != outBuffer->getNumSamples()) {
		ServerLogger::errorln("Error: A client uses wrong buffer size of " + String(audioData->audioBuffer()->getNumSamples()) + " instead of " + String(outBuffer->getNumSamples()));
		return;
	}
	// Loop over the input channels, and add them to either the left or right or both channels!
	auto channelSetup = audioData->channelSetup();
	bool wantsEcho = !channelSetup.isLocalMonitoringDontSendEcho;
	for (int channel = 0; channel < audioData->audioBuffer()->getNumChannels(); channel++) {
		JammerNetzSingleChannelSetup setup = channelSetup.channels[(size_t)channel];
		switch (setup.target) {
		case Mute:
			// Nothing to be done
			break;
		case SendLeft:
			if (isForSender) {
				// Never send this back
				break;
			}
			// Fall-though
		case Left:
			if (isForSender && !wantsEcho) {
				// Don't send this back if it is not requested
				break;
			}
			// This is a left channel, going into the left.
			outBuffer->addFrom(0, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		case SendRight:
			if (isForSender) {
				// Never send this back
				break;
			}
			// Fall-though
		case Right:
			if (isForSender && !wantsEcho) {
				// Don't send this back if it is not requested
				break;
			}
			// And the same for the right channel
			outBuffer->addFrom(1, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		case SendMono:
			if (isForSender) {
				// Never send this back
				break;
			}
			// Fall-through on purpose, treat it as Mono
		case Mono:
			if (isForSender && !wantsEcho) {
				// Don't send this back if it is not requested
				break;
			}
			outBuffer->addFrom(0, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			outBuffer->addFrom(1, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		}
	}
	// We have a mix down now, hand this off to the recorder
	//recorder_.saveBlock(outBuffer->getArrayOfReadPointers(), outBuffer->getNumSamples());
}
