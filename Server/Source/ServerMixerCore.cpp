/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerMixerCore.h"

#include "BuffersConfig.h"

#include <algorithm>
#include <iterator>
#include <utility>

ServerMixerCore::ServerMixerCore(JammerNetzChannelSetup mixdownSetup)
	: mixdownSetup_(std::move(mixdownSetup))
{
}

ServerMixStepResult ServerMixerCore::mix(const ServerInputPackets& incoming)
{
	ServerMixRecipients recipients;
	for (const auto& [name, packet] : incoming) {
		recipients.emplace(name, ClientMixMetadata {
			packet->timestamp(), packet->channelSetup(), packet->protocolVersion()
		});
	}
	return mix(incoming, recipients, incoming.empty() ? std::string() : incoming.begin()->first);
}

ServerMixStepResult ServerMixerCore::mix(const ServerInputPackets& incoming,
	const ServerMixRecipients& recipients,
	const std::string& cadenceClient)
{
	ServerMixStepResult result;
	result.serverTime = serverTime_;
	result.mixSequence = mixSequence_;
	if (incoming.empty() || recipients.empty()) {
		return result;
	}

	const auto firstAudio = incoming.begin()->second->audioBuffer();
	const int bufferLength = firstAudio->getNumSamples();
	serverTime_ += static_cast<uint64>(bufferLength);
	++mixSequence_;
	result.serverTime = serverTime_;
	result.mixSequence = mixSequence_;
	result.outgoing.reserve(recipients.size());
	(void) cadenceClient;

	for (const auto& receiver : recipients) {
		auto output = std::make_shared<AudioBuffer<float>>(2, bufferLength);
		output->clear();
		JammerNetzChannelSetup sessionSetup(false);
		float maximumBpm = 0.0f;
		MidiSignal midiSignal = MidiSignal_None;

		for (const auto& client : incoming) {
			bufferMixdown(*output, *client.second, client.first == receiver.first, result.diagnostics);
			maximumBpm = std::max(maximumBpm, client.second->bpm());
			if (client.second->midiSignal() == MidiSignal_Start && midiSignal == MidiSignal_None) {
				midiSignal = MidiSignal_Start;
			}
			if (client.second->midiSignal() == MidiSignal_Stop) {
				midiSignal = MidiSignal_Stop;
			}
		}
		for (const auto& participant : recipients) {
			if (participant.first == receiver.first) {
				continue;
			}
			std::copy(participant.second.channelSetup.channels.cbegin(),
				participant.second.channelSetup.channels.cend(),
				std::back_inserter(sessionSetup.channels));
		}

		if (maximumBpm > 0.0f) {
			lastBpm_ = maximumBpm;
		}
		result.outgoing.emplace_back(receiver.first, AudioBlock(
			receiver.second.timestamp,
			mixSequence_,
			serverTime_,
			lastBpm_,
			midiSignal,
			SAMPLE_RATE,
			mixdownSetup_,
			std::move(output)),
			std::move(sessionSetup),
			receiver.second.protocolVersion);
	}

	return result;
}

void ServerMixerCore::bufferMixdown(AudioBuffer<float>& output,
	const JammerNetzAudioData& audioData,
	const bool isForSender,
	std::vector<std::string>& diagnostics)
{
	const auto audio = audioData.audioBuffer();
	if (audio->getNumChannels() == 0) {
		diagnostics.emplace_back("Got audio block with no channels, somebody needs to setup his interface");
	}
	if (audio->getNumSamples() != output.getNumSamples()) {
		diagnostics.emplace_back("Error: A client uses wrong buffer size of "
			+ std::to_string(audio->getNumSamples()) + " instead of "
			+ std::to_string(output.getNumSamples()));
		return;
	}

	const auto channelSetup = audioData.channelSetup();
	const bool wantsEcho = !channelSetup.isLocalMonitoringDontSendEcho;
	const auto audioChannelCount = static_cast<size_t>(audio->getNumChannels());
	const auto configuredChannelCount = channelSetup.channels.size();
	if (audioChannelCount != configuredChannelCount) {
		diagnostics.emplace_back("Error: A client sent " + std::to_string(audioChannelCount)
			+ " audio channels but declared " + std::to_string(configuredChannelCount)
			+ " channel setups");
	}
	const auto channelsToMix = static_cast<int>(std::min(audioChannelCount, configuredChannelCount));
	for (int channel = 0; channel < channelsToMix; ++channel) {
		const auto setup = channelSetup.channels[static_cast<size_t>(channel)];
		switch (setup.target) {
		case Mute:
			break;
		case SendLeft:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Left:
			if (!isForSender || wantsEcho) {
				output.addFrom(0, 0, *audio, channel, 0, audio->getNumSamples(), setup.volume);
			}
			break;
		case SendRight:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Right:
			if (!isForSender || wantsEcho) {
				output.addFrom(1, 0, *audio, channel, 0, audio->getNumSamples(), setup.volume);
			}
			break;
		case SendMono:
			if (isForSender) {
				break;
			}
			[[fallthrough]];
		case Mono:
			if (!isForSender || wantsEcho) {
				output.addFrom(0, 0, *audio, channel, 0, audio->getNumSamples(), setup.volume);
				output.addFrom(1, 0, *audio, channel, 0, audio->getNumSamples(), setup.volume);
			}
			break;
		}
	}
}
