/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JoinStageDialog.h"

#include "LayoutConstants.h"

#include "BuffersConfig.h"

#include "ApplicationState.h"
#include "Data.h"

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
}

JoinStageDialog::JoinStageDialog(std::shared_ptr<DataStore> store) : store_(store),
	stageTable_({ "Name", "Audio Type", "Description"}, {}, nullptr)
{
	setLookAndFeel(&dsLookAndFeel_);

	addAndMakeVisible(stageTable_);
	stageTable_.rowSelectedHandler = [this](int row) {
		if (row >= 0 && row < stagesInTable_.size()) {
			selectedStage_ = stagesInTable_[row];
			joinButton_.setEnabled(selectedStage_->audioType == "jammer");
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

	listeners_.push_back(std::make_unique<ValueListener>(Data::getEphemeralPropertyAsValue(EPHEMERAL_DS_VALUE_STAGE_ID), [this](Value& newValue) {
		updateSelectedStage();
	}));
	MessageManager::callAsync([this]() {
		for_each(listeners_.begin(), listeners_.end(), [](std::unique_ptr<ValueListener>& ptr) { ptr->triggerOnChanged();  });
	});

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
	updateSelectedStage();
}

void JoinStageDialog::updateSelectedStage()
{
	if (store_->isOnStage()) {
		auto currentStageId = store_->currentStageID();
		if (currentStageId.has_value()) {
			std::string id = *currentStageId;
			for (int i = 0; i < stagesInTable_.size(); i++) {
				if (stagesInTable_[i]._id == id) {
					stageTable_.selectRow(i);
					return;
				}
			}
		}
		jassertfalse;
	}
	stageTable_.clearSelection();
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
