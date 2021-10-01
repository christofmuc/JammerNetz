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
	joinButton_.onClick = [this, store]() {
		if (selectedStage_.has_value()) {
			sWindow_->exitModalState(1);
			store->join(selectedStage_->_id);
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

bool JoinStageDialog::isCurrentlyOpen()
{
	return sJoinStageDialog && sJoinStageDialog->isShowing();
}

void JoinStageDialog::setStages(std::vector<DigitalStage::Types::Stage> const& stages)
{
	stagesInTable_ = stages; // We need to store this has those in the "DataStore" could be changed at any time via WebSockets
	stageTable_.updateData(stages);
	joinButton_.setEnabled(false);
}

void JoinStageDialog::showDialog(std::shared_ptr<DataStore> store)
{
	if (!sJoinStageDialog) {
		sJoinStageDialog = std::make_unique<JoinStageDialog>(store);
	}
	sJoinStageDialog->setStages(store->allStages());

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
