/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ChannelControllerGroup.h"

ChannelControllerGroup::ChannelControllerGroup()
{
}

void ChannelControllerGroup::setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource)
{
	channelControllers_.clear(true);
	int i = 0;
	for (const auto& channelName : setup->activeChannelNames) {
		auto controller = new ChannelController(channelName, "Input" + String(i), true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
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
		auto controller = new ChannelController(channel.name, "Session" + String(i), true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
		controller->setVolume(channel.volume * 100.0f);
		controller->setTarget(channel.target);
		controller->setMeterSource(meterSource, i++);
	}
	enableClientSideControls(false);
	resized();
}

void ChannelControllerGroup::enableClientSideControls(bool enabled)
{
	for (auto c : channelControllers_) {
		c->enableVolumeSlider(enabled);
		c->enableTargetSelector(enabled);
	}
}

JammerNetzChannelTarget ChannelControllerGroup::getCurrentTarget(int channel) const
{
	if (channel < channelControllers_.size())
		return channelControllers_[channel]->getCurrentTarget();
	jassertfalse;
	return JammerNetzChannelTarget::Mute;
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

int ChannelControllerGroup::numChannels() const
{
	return channelControllers_.size();
}
