/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerService.h"


JammerService::JammerService(std::function<void(std::shared_ptr<JammerNetzAudioData>)> newDataHandler)
{
	// We will send data to the server via this port
	int randomPort = 8888 + (Random().nextInt() % 64);
	if (!socket_.bindToPort(randomPort, "0.0.0.0")) {
		//TODO - there should be no UI initiated from here, probably more something like a fatal log message that should be handled elsewhere.
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Fatal error", "Couldn't bind to port " + String(randomPort));
	}

	// Create the sender 
	sender_ = std::make_unique<Client>(socket_);

	// Fire up the network listener thread which will receive the answers from the server
	receiver_ = std::make_unique<DataReceiveThread>(socket_, newDataHandler);
	receiver_->startThread();
}

JammerService::~JammerService()
{
	// Give the network thread a moment to exit
	receiver_->stopThread(2000);
	socket_.shutdown();
}

Client* JammerService::sender()
{
	return sender_.get();
}

DataReceiveThread* JammerService::receiver()
{
	return receiver_.get();
}
