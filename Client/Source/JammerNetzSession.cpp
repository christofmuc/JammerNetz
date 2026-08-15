/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzSession.h"


bool JammerNetzSession::start(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler,
	const JammerNetzSessionConfiguration& configuration)
{
	if (!shutdown_.exchange(false, std::memory_order_acq_rel)) {
		updateConfiguration(configuration);
		return isAvailable();
	}
	startupError_.clear();
	socket_ = std::make_unique<juce::DatagramSocket>();

	// We will send data to the server via this port
	const int firstPortOffset = Random().nextInt(64);
	bool bound = false;
	for (int attempt = 0; attempt < 64 && !bound; ++attempt) {
		const int port = 8888 + ((firstPortOffset + attempt) % 64);
		bound = socket_->bindToPort(port, "0.0.0.0");
	}
	if (!bound) {
		startupError_ = "Could not bind to any JammerNetz client port (8888-8951)";
		std::cerr << startupError_ << std::endl;
		shutdown_.store(true, std::memory_order_release);
		socket_.reset();
		return false;
	}

	// Create the sender
	sender_ = std::make_unique<Client>(*socket_);

	// Fire up the network listener thread which will receive the answers from the server
	receiver_ = std::make_unique<DataReceiveThread>(*socket_, std::move(newDataHandler));
	updateConfiguration(configuration);
	receiver_->startThread();
	return true;
}

JammerNetzSession::~JammerNetzSession()
{
	shutdown();
}

void JammerNetzSession::shutdown()
{
	if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
		return;
	}
	if (receiver_) {
		receiver_->signalThreadShouldExit();
	}
	if (socket_) {
		socket_->shutdown();
	}
	if (receiver_ && !receiver_->waitForThreadToExit(2000)) {
		std::cerr << "JammerNetz receiver thread did not stop within two seconds; waiting for a clean exit" << std::endl;
		receiver_->waitForThreadToExit(-1);
	}
	receiver_.reset();
	sender_.reset();
	socket_.reset();
}

void JammerNetzSession::updateConfiguration(const JammerNetzSessionConfiguration& configuration)
{
	if (sender_) {
		sender_->setServer(configuration.serverName, configuration.serverPort, configuration.useLocalhost);
		if (configuration.cryptoKey) {
			sender_->setCryptoKey(configuration.cryptoKey->getData(), static_cast<int>(configuration.cryptoKey->getSize()));
		} else {
			sender_->setCryptoKey(nullptr, 0);
		}
		sender_->setUseFEC(configuration.useFEC);
	}
	if (receiver_) {
		if (configuration.cryptoKey) {
			receiver_->setCryptoKey(configuration.cryptoKey->getData(), static_cast<int>(configuration.cryptoKey->getSize()));
		} else {
			receiver_->setCryptoKey(nullptr, 0);
		}
	}
}

Client* JammerNetzSession::sender()
{
	return sender_.get();
}

DataReceiveThread* JammerNetzSession::receiver()
{
	return receiver_.get();
}

bool JammerNetzSession::isReceivingData() const
{
	return receiver_ && receiver_->isReceivingData();
}

double JammerNetzSession::currentRTT() const
{
	return receiver_ ? receiver_->currentRTT() : 0.0;
}

std::shared_ptr<JammerNetzClientInfoMessage> JammerNetzSession::getClientInfo() const
{
	return receiver_ ? receiver_->getClientInfo() : nullptr;
}

JammerNetzChannelSetup JammerNetzSession::getCurrentSessionSetup() const
{
	return receiver_ ? receiver_->sessionSetup() : JammerNetzChannelSetup(false);
}

uint64_t JammerNetzSession::receiveErrorCount() const
{
	return receiver_ ? receiver_->receiveErrorCount() : 0;
}

bool JammerNetzSession::isAvailable() const
{
	return !shutdown_.load(std::memory_order_acquire) && sender_ != nullptr && receiver_ != nullptr;
}

juce::String JammerNetzSession::startupError() const
{
	return startupError_;
}
