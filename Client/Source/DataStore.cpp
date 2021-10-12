/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DataStore.h"

#include "DSConfig.h"
#include "JuceHeader.h"

#include "ServerInfo.h"
#include "BuffersConfig.h"
#include "Logger.h"

#include "Data.h"
#include "ApplicationState.h"

#include <fmt/format.h>

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


DataStore::DataStore(DigitalStage::Auth::string_t const& apiToken) : gotReadySignal_(false), joined_(false)
{
	registerClient(apiToken);
}

bool DataStore::isReady() const
{
	return gotReadySignal_;
}

bool DataStore::isOnStage() const
{
	return joined_;
}

std::optional<std::string> DataStore::currentStageID() const
{
	return client_->getStore()->getStageId();
}

std::vector<DigitalStage::Types::Stage> DataStore::allStages() const
{
	return client_->getStore()->getStages();
}

void DataStore::join(std::string stageId)
{
	nlohmann::json event;
	event["stageId"] = stageId;
	client_->send("join-stage", event);
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
			gotReadySignal_ = true;
		});

		client_->localUserReady.connect([this](User const user, const DigitalStage::Api::Store* store) {
			Data::instance().get().setProperty(VALUE_USER_NAME, String(user.name), nullptr);
		});

		/*client->deviceAdded.connect(handleDeviceAdded);
		client->deviceChanged.connect(handleDeviceChanged);
		client->deviceRemoved.connect(handleDeviceRemoved);
		client->localDeviceReady.connect(handleLocalDeviceReady);*/

		client_->stageJoined.connect([this](const std::string& stageID, const auto& groupID, const DigitalStage::Api::Store* s) {
			SimpleLogger::instance()->postMessage(fmt::format("Entering stage {}", stageID));
			Data::instance().getEphemeral().setProperty(EPHEMERAL_DS_VALUE_STAGE_ID, String(stageID), nullptr);

			// We joined a stage, make sure to issue an onJoin callback that will connect the client to the server
			if (onJoin_) {
				ServerInfo serverInfo;
				auto selectedStage_ = s->getStage(stageID);
				serverInfo.serverName = selectedStage_->jammerIpv4.value_or("");
				serverInfo.serverPort = String(selectedStage_->jammerPort.value_or(7777)).toStdString();
				std::string cryptoKeyAsHex = selectedStage_->jammerKey.value_or("");
				if (!cryptoKeyAsHex.empty()) {
					MemoryBlock cryptoKey;
					cryptoKey.loadFromHexString(cryptoKeyAsHex);
					if (cryptoKey.getSize() != 72) {
						AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Wrong crypto key", "The key from the server has the wrong length!");
					}
					else {
						// For now, write this into a temporary file and put the path into the serverinfo struct
						File file = File::createTempFile(".crypt");
						FileOutputStream out(file);
						out.write(cryptoKey.getData(), cryptoKey.getSize());
						serverInfo.cryptoKeyfilePath = file.getFullPathName().toStdString();
					}
				}
				serverInfo.bufferSize = SAMPLE_BUFFER_SIZE; // This is currently compiled into the software
				serverInfo.sampleRate = SAMPLE_RATE; // This is currently compiled into the software
				joined_ = true;
				onJoin_(serverInfo);
			}
		});
		client_->stageLeft.connect([this](const DigitalStage::Api::Store* s) {
			SimpleLogger::instance()->postMessage("Leaving current stage");
			Data::instance().getEphemeral().setProperty(EPHEMERAL_DS_VALUE_STAGE_ID, "", nullptr);
			joined_ = false;
			if (onLeave_) {
				onLeave_();
			}
		});
		client_->stageChanged.connect([this](const std::string& stageID, nlohmann::json partialJson,const DigitalStage::Api::Store* s) {
			SimpleLogger::instance()->postMessage("Got stage change message");
		});

		// Always print on stage changes
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
