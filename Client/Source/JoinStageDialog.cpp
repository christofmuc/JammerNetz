/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JoinStageDialog.h"

#include "LayoutConstants.h"

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

JoinStageDialog::JoinStageDialog(std::shared_ptr<DataStore> store) :
	stageTable_({ "Name", "Audio Type", "Description"}, {}, [this](int) {})
{
	setLookAndFeel(&dsLookAndFeel_);

	addAndMakeVisible(stageTable_);
	setSize(400, 300);
}

JoinStageDialog::~JoinStageDialog() {
	setLookAndFeel(nullptr);
}

void JoinStageDialog::resized()
{
	auto area = getLocalBounds();
	stageTable_.setBounds(area.reduced(kNormalInset));
}

void JoinStageDialog::setStages(std::vector<DigitalStage::Types::Stage> const& stages)
{
	stageTable_.updateData(stages);
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

void JoinStageDialog::showDialog(std::shared_ptr<DataStore> store)
{
	if (!sJoinStageDialog) {
		sJoinStageDialog = std::make_unique<JoinStageDialog>(store);
	}
	//sLoginDialog->callback_ = callback;
	// Populate table!
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
