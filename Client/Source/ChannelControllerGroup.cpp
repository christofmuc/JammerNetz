/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ChannelControllerGroup.h"

#include <map>
#include <utility>

ChannelControllerGroup::ChannelControllerGroup()
{
}

void ChannelControllerGroup::setup(std::shared_ptr<ChannelSetup> setup, FFAU::LevelMeterSource*meterSource)
{
	channelControllers_.clear(true);
	sessionChannelIdentities_.clear();
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
	sessionChannelIdentities_.clear();
	std::map<std::pair<uint32, uint16>, int> identityCounts;
	for (const auto& channel : sessionChannels->channels) {
		identityCounts[{channel.sourceClientId, channel.sourceChannelIndex}]++;
	}

	int i = 0;
	for (const auto& channel : sessionChannels->channels) {
		auto controller = new ChannelController(channel.name, "Session" + String(i), true, true, true);
		addAndMakeVisible(controller);
		channelControllers_.add(controller);
		controller->setVolume(channel.volume * 100.0f);
		controller->setTarget(channel.target);
		controller->setMeterSource(meterSource, i);
		const auto identity = std::make_pair(channel.sourceClientId, channel.sourceChannelIndex);
		sessionChannelIdentities_.push_back(identity);
		// Only enable remote control for identities that map to exactly one visible slider.
		// If an identity appears multiple times, controlling it would be ambiguous.
		auto canControlRemote = identity.first != 0 && identityCounts[identity] == 1;
		controller->enableVolumeSlider(canControlRemote);
		controller->enableTargetSelector(false);
		if (canControlRemote) {
			controller->setUpdateHandler([this, identity](double volumePercent, JammerNetzChannelTarget) {
				if (sessionVolumeChangedHandler_) {
					sessionVolumeChangedHandler_(identity.first, identity.second, (float) volumePercent);
				}
			});
		}
		i++;
	}
	resized();
}

void ChannelControllerGroup::setSessionVolumeChangedHandler(SessionVolumeChangedHandler handler)
{
	sessionVolumeChangedHandler_ = std::move(handler);
}

void ChannelControllerGroup::setSessionChannelVolume(int channel, float volumePercent)
{
	if (channel >= 0 && channel < channelControllers_.size()) {
		channelControllers_[channel]->setVolume(volumePercent);
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

bool ChannelControllerGroup::isAnyVolumeSliderBeingDragged() const
{
	for (auto controller : channelControllers_) {
		if (controller->isVolumeSliderBeingDragged()) {
			return true;
		}
	}
	return false;
}

bool ChannelControllerGroup::isSessionVolumeSliderBeingDragged(uint32 sourceClientId, uint16 sourceChannelIndex) const
{
	auto channelIndex = findSessionChannelIndex(sourceClientId, sourceChannelIndex);
	if (!channelIndex.has_value() || *channelIndex < 0 || *channelIndex >= channelControllers_.size()) {
		jassertfalse;
		return false;
	}
	return channelControllers_[*channelIndex]->isVolumeSliderBeingDragged();
}

std::optional<int> ChannelControllerGroup::findSessionChannelIndex(uint32 sourceClientId, uint16 sourceChannelIndex) const
{
	for (size_t i = 0; i < sessionChannelIdentities_.size(); i++) {
		if (sessionChannelIdentities_[i] == std::make_pair(sourceClientId, sourceChannelIndex)) {
			return static_cast<int>(i);
		}
	}
	return {};
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
