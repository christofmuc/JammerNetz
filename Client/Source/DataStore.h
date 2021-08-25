/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "DigitalStage/Auth/AuthService.h"
#include "DigitalStage/Api/Client.h"

class DataStore {
public:
	DataStore(DigitalStage::Auth::string_t const& apiToken);

	bool isReady() const;

	std::vector<DigitalStage::Types::Stage> allStages() const;

private:
	void registerClient(DigitalStage::Auth::string_t const& apiToken);

	std::unique_ptr< DigitalStage::Api::Client> client_;
	std::atomic<bool> gotReadySignal_;
};


class DataStorePopulationWindow : public ThreadWithProgressWindow {
public:
	DataStorePopulationWindow(std::shared_ptr<DataStore> store);

	virtual void run() override;

private:
	std::shared_ptr<DataStore> store_;
};