/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerService.h"


JammerService::JammerService(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
{
	// We will send data to the server via this port
	const int firstPortOffset = Random().nextInt(64);
	bool bound = false;
	for (int attempt = 0; attempt < 64 && !bound; ++attempt) {
		const int port = 8888 + ((firstPortOffset + attempt) % 64);
		bound = socket_.bindToPort(port, "0.0.0.0");
	}
	if (!bound) {
		startupError_ = "Could not bind to any JammerNetz client port (8888-8951)";
		std::cerr << startupError_ << std::endl;
		return;
	}

	// Create the sender
	sender_ = std::make_unique<Client>(socket_);

	// Fire up the network listener thread which will receive the answers from the server
	receiver_ = std::make_unique<DataReceiveThread>(socket_, newDataHandler);
	receiver_->startThread();
}

JammerService::~JammerService()
{
	shutdown();
}

void JammerService::shutdown()
{
	if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
		return;
	}
	if (receiver_) {
		receiver_->signalThreadShouldExit();
	}
	socket_.shutdown();
	if (receiver_ && !receiver_->stopThread(2000)) {
		std::cerr << "JammerNetz receiver thread did not stop cleanly" << std::endl;
	}
}

Client* JammerService::sender()
{
	return sender_.get();
}

DataReceiveThread* JammerService::receiver()
{
	return receiver_.get();
}

bool JammerService::isReceivingData() const
{
	return receiver_ && receiver_->isReceivingData();
}

double JammerService::currentRTT() const
{
	return receiver_ ? receiver_->currentRTT() : 0.0;
}

std::shared_ptr<JammerNetzClientInfoMessage> JammerService::getClientInfo() const
{
	return receiver_ ? receiver_->getClientInfo() : nullptr;
}

JammerNetzChannelSetup JammerService::getCurrentSessionSetup() const
{
	return receiver_ ? receiver_->sessionSetup() : JammerNetzChannelSetup(false);
}

uint64_t JammerService::receiveErrorCount() const
{
	return receiver_ ? receiver_->receiveErrorCount() : 0;
}

bool JammerService::isAvailable() const
{
	return sender_ != nullptr && receiver_ != nullptr;
}

juce::String JammerService::startupError() const
{
	return startupError_;
}
