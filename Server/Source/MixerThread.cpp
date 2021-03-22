/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "MixerThread.h"

#include "BuffersConfig.h"

MixerThread::MixerThread(TPacketStreamBundle &incoming, JammerNetzChannelSetup mixdownSetup, TOutgoingQueue &outgoing, TMessageQueue &wakeUpQueue, Recorder &recorder, ServerBufferConfig bufferConfig)
	: Thread("MixerThread"), incoming_(incoming), mixdownSetup_(mixdownSetup), outgoing_(outgoing), wakeUpQueue_(wakeUpQueue), recorder_(recorder), bufferConfig_(bufferConfig)
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
				if (inqueue.second->size() > bufferConfig_.serverIncomingJitterBuffer) available++;
				if (inqueue.second->size() > bufferConfig_.serverIncomingMaximumBuffer) queueOverrun = true; // This is one client much faster than the others
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
		std::vector<std::string> toBeRemoved;
		for (auto inClient = incoming_.cbegin(); inClient != incoming_.cend(); inClient++) {
			if (inClient->second) {
				if (incomingData.find(inClient->first) == incomingData.end()) {
					std::cout << "Client " << inClient->first << " hasn't delivered any packets for a while, removing from client list!" << std::endl;
					toBeRemoved.push_back(inClient->first);
					for (auto &streamData : incoming_) {
						if (streamData.second) {
							std::cout << streamData.second->qualityStatement() << std::endl;
						}
					}
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

			// For each client that has delivered data, produce a mix down package and send it back
			//TODO - also the clients that have not provided data should get a package with a note that they are not contained within - they could do a local fill in.
			for (auto &receiver : incomingData) {
				std::shared_ptr<AudioBuffer<float>> outBuffer = std::make_shared<AudioBuffer<float>>(2, bufferLength);
				outBuffer->clear();

				// We now produce one mix for each client, specific, because you might not want to hear your own voice microphone
				for (auto &client : incomingData) {
					//recorder_.saveBlock(client.second->audioBuffer()->getArrayOfReadPointers(), outBuffer->getNumSamples());
					bufferMixdown(outBuffer, client.second, client.first == receiver.first);
				}

				// The outgoing queue takes packages for all clients, they will be sent out to different addresses
				OutgoingPackage package(receiver.first, AudioBlock(
					receiver.second->timestamp(),
					receiver.second->messageCounter(), 
					48000,
					mixdownSetup_,
					outBuffer
					));
				if (!outgoing_.try_push(package)) {
					// That's a bad sign - I would assume the sender thread died and that's possibly because the network is down. 
					// Abort
					std::cerr << "send queue length overflow at " << outgoing_.size() << " packets - network down? FATAL!" << std::endl;
					exit(-1);
				}
				/*		// Write this to the recorder
						if (!clientRecorder_.isRecording()) {
							//TODO - this needs to compare if it has changed / is different from the one currently being written
							clientRecorder_.updateChannelInfo(48000, audioData->channelSetup());
						}
						clientRecorder_.saveBlock(audioData->audioBuffer_->getArrayOfReadPointers(), audioData->audioBuffer_->getNumSamples());*/
			}
		}
	}
}

void MixerThread::bufferMixdown(std::shared_ptr<AudioBuffer<float>> &outBuffer, std::shared_ptr<JammerNetzAudioData> const &audioData, bool isForSender) {
	if (audioData->audioBuffer()->getNumChannels() == 0) { 
		std::cout << "Got audio block with no channels, somebody needs to setup his interface" << std::endl;
	}
	if (audioData->audioBuffer()->getNumSamples() != outBuffer->getNumSamples()) {
		std::cerr << "Error: A client uses wrong buffer size of " << audioData->audioBuffer()->getNumSamples() << " instead of " << outBuffer->getNumSamples() << std::endl;
		return;
	}
	// Loop over the input channels, and add them to either the left or right or both channels!
	for (int channel = 0; channel < audioData->audioBuffer()->getNumChannels(); channel++) {
		JammerNetzSingleChannelSetup setup = audioData->channelSetup().channels[channel];
		switch (setup.target) {
		case Unused:
			// Nothing to be done, ignore this channel; This is the same as Mute
			break;
		case Left:
			// This is a left channel, going into the left. 
			outBuffer->addFrom(0, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		case Right:
			// And the same for the right channel
			outBuffer->addFrom(1, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		case SendOnly:
			if (isForSender) {
				// Don't send this back, we don't want to hear our own voice talking
				break;
			}
			// Fall-through on purpose, treat it as Mono
		case Mono:
			outBuffer->addFrom(0, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			outBuffer->addFrom(1, 0, *audioData->audioBuffer(), channel, 0, audioData->audioBuffer()->getNumSamples(), setup.volume);
			break;
		}
	}
	// We have a mix down now, hand this off to the recorder
	//recorder_.saveBlock(outBuffer->getArrayOfReadPointers(), outBuffer->getNumSamples());
}
