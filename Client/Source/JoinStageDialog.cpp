/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JoinStageDialog.h"

#include "LayoutConstants.h"

#include "BuffersConfig.h"

static std::unique_ptr<JoinStageDialog> sJoinStageDialog;
static juce::DialogWindow* sWindow_;

// Implement vistor to populate SimpleTable class from our data store object
template <>
void visit(DigitalStage::Types::Stage const& dataItem, int column, std::function<void(std::string const&)> visitor) {
	switch (column) {
	case 1: visitor(dataItem.name); break;
	case 2: visitor(dataItem.audioType); break;
	case 3: visitor(dataItem.description); break;
	default: jassertfalse;
	}
}

static void dialogClosed(int modalResult, JoinStageDialog* dialog)
{
	if (modalResult == 1 && dialog != nullptr) { // (must check that dialog isn't null in case it was deleted..)
		//dialog->triggerCallback();
	}
	else {
		// Didn't join a stage - quit application?
		JUCEApplication::quit();
	}
}

JoinStageDialog::JoinStageDialog(std::shared_ptr<DataStore> store) :
	stageTable_({ "Name", "Audio Type", "Description"}, {}, nullptr)
{
	setLookAndFeel(&dsLookAndFeel_);

	addAndMakeVisible(stageTable_);
	stageTable_.rowSelectedHandler = [this](int row) {
		if (row >= 0 && row < stagesInTable_.size()) {
			selectedStage_ = stagesInTable_[row];
			joinButton_.setEnabled(selectedStage_->audioType == "jammer");
		}
		else {
			jassertfalse;
		}
	};

	joinButton_.setButtonText("Join");
	addAndMakeVisible(&joinButton_);
	joinButton_.setEnabled(false);
	joinButton_.onClick = [this]() {
		if (joinHandler && selectedStage_.has_value()) {
			ServerInfo serverInfo;
			if (true) { //selectedStage_->jammerIpv4.has_value() && selectedStage_->jammerKey.has_value() && selectedStage_->jammerPort.has_value()) {
				serverInfo.serverName = selectedStage_->jammerIpv4.value_or("");
				serverInfo.serverPort = String(selectedStage_->jammerPort.value_or(7777)).toStdString();
				serverInfo.cryptoKeyfilePath = selectedStage_->jammerKey.value_or(""); //TODO This won't work - the jammerkey should either be an uuencoded secret, or an URL to download?
				serverInfo.bufferSize = SAMPLE_BUFFER_SIZE; // This is currently compiled into the software
				serverInfo.sampleRate = SAMPLE_RATE; // This is currently compiled into the software
				sWindow_->exitModalState(1);
				joinHandler(serverInfo);
			}
			else {
				AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Stage incomplete", "The selected stage is not equipped with all required data items to connect to. Please contact your administrator!");
			}
		}
	};
	setSize(400, 300);
}

JoinStageDialog::~JoinStageDialog() {
	setLookAndFeel(nullptr);
}

void JoinStageDialog::resized()
{
	auto area = getLocalBounds().reduced(kNormalInset);
	auto buttonRow = area.removeFromBottom(kLineSpacing);
	joinButton_.setBounds(buttonRow.withSizeKeepingCentre(kButtonWidth, kLineHeight));
	stageTable_.setBounds(area.withTrimmedBottom(kNormalInset));
}

void JoinStageDialog::setStages(std::vector<DigitalStage::Types::Stage> const& stages)
{
	stagesInTable_ = stages; // We need to store this has those in the "DataStore" could be changed at any time via WebSockets
	stageTable_.updateData(stages);
	joinButton_.setEnabled(false);
}

void JoinStageDialog::showDialog(std::shared_ptr<DataStore> store, std::function<void(ServerInfo serverInfo)> joinHandler)
{
	if (!sJoinStageDialog) {
		sJoinStageDialog = std::make_unique<JoinStageDialog>(store);
	}
	sJoinStageDialog->setStages(store->allStages());
	sJoinStageDialog->joinHandler = joinHandler;

	DialogWindow::LaunchOptions launcher;
	launcher.content.set(sJoinStageDialog.get(), false);
	launcher.componentToCentreAround = juce::TopLevelWindow::getTopLevelWindow(0)->getTopLevelComponent();
	launcher.dialogTitle = "Select Digital Stage to join";
	launcher.useNativeTitleBar = false;
	launcher.dialogBackgroundColour = Colours::black;
	sWindow_ = launcher.launchAsync();
	ModalComponentManager::getInstance()->attachCallback(sWindow_, ModalCallbackFunction::forComponent(dialogClosed, sJoinStageDialog.get()));
}

void JoinStageDialog::release()
{
	sJoinStageDialog.reset();
}
