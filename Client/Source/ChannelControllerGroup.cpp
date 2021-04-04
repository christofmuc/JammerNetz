/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ChannelControllerGroup.h"

ChannelControllerGroup::ChannelControllerGroup()
{
}

void ChannelControllerGroup::setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource, std::function<void(std::shared_ptr<ChannelSetup>)> callback)
{
	channelControllers_.clear(true);
	int i = 0;
	for (const auto& channelName : setup->activeChannelNames) {
		auto controller = new ChannelController(channelName, "Input" + String(i), [this, callback, setup](double newVolume, JammerNetzChannelTarget newTarget) {
			ignoreUnused(newVolume, newTarget);
			callback(setup);
		}, true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
		controller->fromData();
		controller->setMeterSource(meterSource, i);
		i++;
	}
	resized();
}

void ChannelControllerGroup::setup(std::shared_ptr<JammerNetzChannelSetup> sessionChannels, FFAU::LevelMeterSource*meterSource)
{
	channelControllers_.clear(true);
	int i = 0;
	for (const auto& channel : sessionChannels->channels) {
		auto controller = new ChannelController(channel.name, Uuid::Uuid().toString(), [](double newVolume, JammerNetzChannelTarget newTarget) {
			ignoreUnused(newVolume, newTarget);
		}, true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
		controller->setVolume(channel.volume * 100.0f);
		controller->setTarget(channel.target);
		controller->setMeterSource(meterSource, i++);
	}
	resized();
}

JammerNetzChannelTarget ChannelControllerGroup::getCurrentTarget(int channel) const
{
	if (channel < channelControllers_.size())
		return channelControllers_[channel]->getCurrentTarget();
	jassertfalse;
	return JammerNetzChannelTarget::Unused;
}

float ChannelControllerGroup::getCurrentVolume(int channel) const
{
	if (channel < channelControllers_.size())
		return channelControllers_[channel]->getCurrentVolume();
	jassertfalse;
	return 0.0f;
}

void ChannelControllerGroup::setPitchDisplayed(int channel, MidiNote note)
{
	if (channel < channelControllers_.size())
		channelControllers_[channel]->setPitchDisplayed(note);
}

void ChannelControllerGroup::resized()
{
	auto inputArea = getLocalBounds();
	int sizePerController = channelControllers_.isEmpty() ? 0 : std::min(inputArea.getWidth() / channelControllers_.size(), 100);
	for (auto controller : channelControllers_) {
		controller->setBounds(inputArea.removeFromLeft(sizePerController));
	}
}

void ChannelControllerGroup::toData()
{
	// Save the position and the name of the sliders to the settings file
	for (auto c : channelControllers_) {
		c->toData();
	}
}
