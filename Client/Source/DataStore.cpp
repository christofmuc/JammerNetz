/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataStore.h"

#include "DSConfig.h"
#include "JuceHeader.h"

void printStage(const DigitalStage::Api::Store* s)
{
	auto stages = s->getStages();
	for (const auto& stage : stages) {
		std::cout << "[" << stage.name << "] " << std::endl;
		auto groups = s->getGroupsByStage(stage._id);
		for (const auto& group : groups) {
			std::cout << "  [" << group.name << "]" << std::endl;
			auto stageMembers = s->getStageMembersByGroup(group._id);
			for (const auto& stageMember : stageMembers) {
				auto user = s->getUser(stageMember.userId);
				std::cout << "    [" << stageMember._id << ": "
					<< (user ? user->name : "") << "]" << std::endl;
				auto stageDevices = s->getStageDevicesByStageMember(stageMember._id);
				for (const auto& stageDevice : stageDevices) {
					auto videoTracks =
						s->getVideoTracksByStageDevice(stageDevice._id);
					for (const auto& videoTrack : videoTracks) {
						std::cout << "      [Video Track " << videoTrack._id << "]"
							<< std::endl;
					}
					auto audioTracks =
						s->getAudioTracksByStageDevice(stageDevice._id);
					for (const auto& audioTrack : audioTracks) {
						std::cout << "      [Audio Track " << audioTrack._id << "]"
							<< std::endl;
					}
				}
			}
		}
	}
}


DataStore::DataStore(DigitalStage::Auth::string_t const& apiToken) : gotReadySignal_(false)
{
	registerClient(apiToken);
}

bool DataStore::isReady() const
{
	return gotReadySignal_;
}

std::vector<DigitalStage::Types::Stage> DataStore::allStages() const
{
	return client_->getStore()->getStages();
}

void DataStore::registerClient(DigitalStage::Auth::string_t const& apiToken)
{
	try {
		nlohmann::json initialDevice;
		initialDevice["uuid"] = "123456";
		initialDevice["type"] = "jammer";
		initialDevice["canAudio"] = true;
		initialDevice["canVideo"] = false;
		client_ = std::make_unique<DigitalStage::Api::Client>(DS_API_SERVER);

		client_->ready.connect([this](const DigitalStage::Api::Store* store) {
			auto stageId = store->getStageDeviceId();
			gotReadySignal_ = true;
		});

		/*client->deviceAdded.connect(handleDeviceAdded);
		client->deviceChanged.connect(handleDeviceChanged);
		client->deviceRemoved.connect(handleDeviceRemoved);
		client->localDeviceReady.connect(handleLocalDeviceReady);
		client->stageJoined.connect(handleStageJoined);
		client->stageLeft.connect(handleStageLeft);*/

		// Always print on stage changes
		client_->stageJoined.connect(
			[](const auto&, const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->stageLeft.connect([](const DigitalStage::Api::Store* s) { printStage(s); });
		client_->groupAdded.connect(
			[](const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->groupChanged.connect(
			[](const auto&, const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->groupRemoved.connect(
			[](const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->stageMemberAdded.connect(
			[](const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->stageMemberChanged.connect(
			[](const auto&, const auto&, const DigitalStage::Api::Store* s) { printStage(s); });
		client_->stageMemberRemoved.connect(
			[](const auto&, const DigitalStage::Api::Store* s) { printStage(s); });

		client_->connect(apiToken, initialDevice);
	}
	catch (std::exception& err) {
		jassertfalse;
		std::cerr << "Got exception: " << err.what() << std::endl;
	}
}

DataStorePopulationWindow::DataStorePopulationWindow(std::shared_ptr<DataStore> store) 
	: ThreadWithProgressWindow("Waiting for data from digital-stage.org", true, true), store_(store)
{
}

void DataStorePopulationWindow::run()
{

}
