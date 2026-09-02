/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioPacketSink.h"
#include "BuffersConfig.h"
#include "ClientState.h"
#include "JammerNetzAudioEngine.h"
#include "JammerNetzPackage.h"
#include "ServerMixScheduler.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr int compatibilityFrameSamples = SAMPLE_BUFFER_SIZE;
constexpr int compatibilityWireCapacity = 65536;
constexpr float silenceEpsilon = 1.0e-7f;

json readJson(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("Cannot open input: " + path);
	}
	json result;
	input >> result;
	return result;
}

void writeJson(const std::string& path, const json& value)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		throw std::runtime_error("Cannot open output: " + path);
	}
	output << value.dump(2) << '\n';
}

std::vector<std::uint8_t> serializeAudio(const JammerNetzAudioData& packet)
{
	std::array<uint8, compatibilityWireCapacity> bytes {};
	std::size_t size = 0;
	packet.serialize(bytes.data(), size);
	if (size == 0 || size > bytes.size()) {
		throw std::runtime_error("Audio serialization produced an invalid size");
	}
	return { bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size) };
}

std::shared_ptr<JammerNetzAudioData> deserializeAudio(const json& encoded)
{
	auto bytes = encoded.get<std::vector<std::uint8_t>>();
	if (bytes.empty()) {
		throw std::runtime_error("Cannot deserialize an empty packet");
	}
	auto message = JammerNetzMessage::deserialize(bytes.data(), bytes.size());
	auto audio = std::dynamic_pointer_cast<JammerNetzAudioData>(message);
	if (!audio) {
		throw std::runtime_error("Serialized compatibility packet is not audio data");
	}
	return audio;
}

JammerNetzChannelTarget parseTarget(const std::string& target)
{
	if (target == "left") {
		return JammerNetzChannelTarget::Left;
	}
	if (target == "right") {
		return JammerNetzChannelTarget::Right;
	}
	if (target == "mono") {
		return JammerNetzChannelTarget::Mono;
	}
	throw std::runtime_error("Unknown channel target: " + target);
}

float sourceSample(const std::uint32_t sourceId, const std::uint64_t sample)
{
	const auto value = (static_cast<std::uint64_t>(sourceId) * 131U + sample * 17U) % 2001U;
	return (static_cast<float>(value) - 1000.0f) / 1000.0f;
}

class CapturingPacketSink final : public AudioPacketSink {
public:
	bool sendData(const JammerNetzChannelSetup& channelSetup,
		std::shared_ptr<AudioBuffer<float>> audioBuffer,
		ControlData controls) override
	{
		auto retained = std::make_shared<AudioBuffer<float>>();
		*retained = *audioBuffer;
		const auto timestamp = 1000.0 * static_cast<double>(frameIndex_ * SAMPLE_BUFFER_SIZE)
			/ static_cast<double>(SAMPLE_RATE);
		packets_.push_back(std::make_shared<JammerNetzAudioData>(
			nextCounter_++, timestamp, channelSetup, SAMPLE_RATE, controls.bpm,
			controls.midiSignal.value_or(MidiSignal_None), std::move(retained), nullptr));
		++frameIndex_;
		return true;
	}

	std::vector<std::shared_ptr<JammerNetzAudioData>> takePackets()
	{
		auto result = std::move(packets_);
		packets_.clear();
		return result;
	}

private:
	uint64 nextCounter_ { 10 };
	std::uint64_t frameIndex_ { 0 };
	std::vector<std::shared_ptr<JammerNetzAudioData>> packets_;
};

int generateClientTrace(const std::string& outputPath, const std::string& clientName,
	const std::uint32_t sourceId, const std::string& route, const int frames)
{
	if (frames <= 0) {
		throw std::runtime_error("Frame count must be positive");
	}

	auto sink = std::make_shared<CapturingPacketSink>();
	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File(), sink);
	JammerNetzChannelSetup setup(true);
	JammerNetzSingleChannelSetup channel(static_cast<uint8>(parseTarget(route)));
	channel.name = clientName;
	setup.channels.push_back(std::move(channel));
	engine.setChannelSetup(setup);
	engine.setLocalMonitoring(false);
	engine.setMasterVolume(1.0);
	engine.setMonitorBalance(1.0);
	engine.prepare(SAMPLE_RATE, compatibilityFrameSamples);

	json ticks = json::array();
	std::uint64_t absoluteSample = 0;
	for (int frame = 0; frame < frames; ++frame) {
		std::array<float, compatibilityFrameSamples> input {};
		std::array<float, compatibilityFrameSamples> left {};
		std::array<float, compatibilityFrameSamples> right {};
		for (auto& sample : input) {
			sample = sourceSample(sourceId, absoluteSample++);
		}
		const float* inputs[] { input.data() };
		float* outputs[] { left.data(), right.data() };
		engine.process(inputs, 1, outputs, 2, compatibilityFrameSamples);
		while (engine.processNextOutgoingPacket()) {}

		json packets = json::array();
		for (const auto& packet : sink->takePackets()) {
			packets.push_back(serializeAudio(*packet));
		}
		ticks.push_back(std::move(packets));
	}
	engine.release();

	writeJson(outputPath, {
		{ "format", 1 },
		{ "peer_version", JAMMERNETZ_COMPAT_VERSION },
		{ "client", clientName },
		{ "source_id", sourceId },
		{ "route", route },
		{ "frames", frames },
		{ "ticks", std::move(ticks) }
	});
	return 0;
}

JammerNetzChannelSetup stereoMixdown()
{
	return JammerNetzChannelSetup(false, {
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left),
		JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Right)
	});
}

json serializeOutgoing(const OutgoingPackage& outgoing)
{
	JammerNetzAudioData packet(outgoing.audioBlock, nullptr);
	if (!JammerNetzProtocol::supportsSplitSessionInfo(outgoing.receiverProtocolVersion)) {
		packet.setLegacySessionSetup(outgoing.sessionSetup);
	}
	return serializeAudio(packet);
}

int mixServerTrace(const std::string& inputPath, const std::string& outputPath)
{
	const auto scenario = readJson(inputPath);
	const auto frames = scenario.at("frames").get<int>();
	const auto& inputs = scenario.at("clients");
	if (frames <= 0 || !inputs.is_array() || inputs.empty()) {
		throw std::runtime_error("Server scenario requires frames and clients");
	}

	TPacketStreamBundle clients;
	json downloads = json::object();
	for (const auto& input : inputs) {
		const auto name = input.at("name").get<std::string>();
		clients.emplace(name, std::make_shared<ClientState>(name));
		downloads[name] = json::array();
		for (int tick = 0; tick < frames; ++tick) {
			downloads[name].push_back(json::array());
		}
	}

	ServerMixScheduler scheduler(stereoMixdown(), { 1, 3, 0 });
	std::uint64_t mixCount = 0;
	std::uint64_t deserializationFailures = 0;
	std::uint64_t outputCount = 0;
	std::uint64_t fastForwardEvents = 0;
	std::uint64_t fastForwardedPackets = 0;
	std::uint64_t cadenceChanges = 0;
	json observations = json::array();

	for (int tick = 0; tick < frames; ++tick) {
		for (const auto& input : inputs) {
			const auto name = input.at("name").get<std::string>();
			const auto& tickPackets = input.at("ticks").at(static_cast<std::size_t>(tick));
			for (const auto& encoded : tickPackets) {
				try {
					auto packet = deserializeAudio(encoded);
					clients.at(name)->push(std::move(packet), 0,
						ClientState::TimePoint{} + std::chrono::microseconds(
							static_cast<std::int64_t>(tick) * 2667));
				}
				catch (const std::exception&) {
					++deserializationFailures;
				}
			}
		}

		for (int wake = 0; wake < 16; ++wake) {
			const auto now = ClientState::TimePoint{} + std::chrono::microseconds(
				static_cast<std::int64_t>(tick) * 2667 + wake);
			auto result = scheduler.process(clients, now);
#ifdef JAMMERNETZ_COMPAT_CANDIDATE
			if (!result.fastForwardedClients.empty()) {
				fastForwardEvents += result.fastForwardedClients.size();
				for (const auto& entry : result.fastForwardedClients) {
					fastForwardedPackets += entry.second.discardedPackets;
				}
			}
			if (result.cadenceClientChanged) {
				++cadenceChanges;
			}
#endif
			if (!result.mix.outgoing.empty()) {
				++mixCount;
			}
			for (const auto& outgoing : result.mix.outgoing) {
				json encoded = serializeOutgoing(outgoing);
				downloads.at(outgoing.targetAddress).at(static_cast<std::size_t>(tick)).push_back(encoded);
				observations.push_back({
					{ "tick", tick },
					{ "target", outgoing.targetAddress },
					{ "message_counter", outgoing.audioBlock.messageCounter },
					{ "timestamp", outgoing.audioBlock.timestamp },
					{ "server_time", outgoing.audioBlock.serverTime }
				});
				++outputCount;
			}
			if (!result.shouldWakeAgain) {
				break;
			}
		}
	}

	writeJson(outputPath, {
		{ "format", 1 },
		{ "peer_version", JAMMERNETZ_COMPAT_VERSION },
		{ "frames", frames },
		{ "downloads", std::move(downloads) },
		{ "observations", std::move(observations) },
		{ "mix_count", mixCount },
		{ "output_count", outputCount },
		{ "deserialization_failures", deserializationFailures },
		{ "fast_forward_events", fastForwardEvents },
		{ "fast_forwarded_packets", fastForwardedPackets },
		{ "cadence_changes", cadenceChanges }
	});
	return deserializationFailures == 0 ? 0 : 2;
}

std::uint64_t updateHash(std::uint64_t hash, const float sample)
{
	hash ^= std::bit_cast<std::uint32_t>(sample);
	return hash * 1099511628211ULL;
}

int renderClientTrace(const std::string& inputPath, const std::string& outputPath)
{
	const auto trace = readJson(inputPath);
	const auto& ticks = trace.at("ticks");
	if (!ticks.is_array()) {
		throw std::runtime_error("Render trace requires a tick array");
	}

	JammerNetzSession session;
	JammerNetzAudioEngine engine(session, juce::File());
	engine.setChannelSetup(JammerNetzChannelSetup(true));
	engine.setLocalMonitoring(false);
	engine.setMasterVolume(1.0);
	engine.setMonitorBalance(1.0);
	engine.setPlayoutBufferRange(1, 128);
	engine.prepare(SAMPLE_RATE, compatibilityFrameSamples);

	std::uint64_t hash = 1469598103934665603ULL;
	std::uint64_t deserializationFailures = 0;
	std::uint64_t deliveredPackets = 0;
	std::uint64_t nonSilentTicks = 0;
	std::uint64_t currentSilenceRun = 0;
	std::uint64_t longestSilenceAfterStart = 0;
	int firstNonSilentTick = -1;
	int lastNonSilentTick = -1;
	bool playbackStarted = false;

	for (std::size_t tick = 0; tick < ticks.size(); ++tick) {
		for (const auto& encoded : ticks.at(tick)) {
			try {
				auto packet = deserializeAudio(encoded);
				engine.enqueueRemoteAudio(std::move(packet));
				++deliveredPackets;
			}
			catch (const std::exception&) {
				++deserializationFailures;
			}
		}
		while (engine.processNextIncomingPacket()) {}

		std::array<float, compatibilityFrameSamples> left {};
		std::array<float, compatibilityFrameSamples> right {};
		float* outputs[] { left.data(), right.data() };
		engine.process(nullptr, 0, outputs, 2, compatibilityFrameSamples);

		bool nonSilent = false;
		for (int sample = 0; sample < compatibilityFrameSamples; ++sample) {
			nonSilent = nonSilent || std::abs(left[static_cast<std::size_t>(sample)]) > silenceEpsilon
				|| std::abs(right[static_cast<std::size_t>(sample)]) > silenceEpsilon;
			hash = updateHash(hash, left[static_cast<std::size_t>(sample)]);
			hash = updateHash(hash, right[static_cast<std::size_t>(sample)]);
		}
		if (nonSilent) {
			++nonSilentTicks;
			firstNonSilentTick = firstNonSilentTick < 0 ? static_cast<int>(tick) : firstNonSilentTick;
			lastNonSilentTick = static_cast<int>(tick);
			playbackStarted = true;
			currentSilenceRun = 0;
		}
		else if (playbackStarted) {
			++currentSilenceRun;
			longestSilenceAfterStart = std::max(longestSilenceAfterStart, currentSilenceRun);
		}
	}

	const auto quality = engine.getPlayoutQualityInfo();
	engine.release();
	writeJson(outputPath, {
		{ "format", 1 },
		{ "peer_version", JAMMERNETZ_COMPAT_VERSION },
		{ "rendered_ticks", ticks.size() },
		{ "delivered_packets", deliveredPackets },
		{ "deserialization_failures", deserializationFailures },
		{ "non_silent_ticks", nonSilentTicks },
		{ "first_non_silent_tick", firstNonSilentTick },
		{ "last_non_silent_tick", lastNonSilentTick },
		{ "longest_silence_after_start", longestSilenceAfterStart },
		{ "play_underruns", quality.playUnderruns_ },
		{ "discarded_packets", quality.discardedPackageCounter_ },
		{ "output_hash", hash }
	});
	return deserializationFailures == 0 ? 0 : 2;
}

void usage()
{
	std::cerr << "Usage:\n"
		<< "  peer generate OUTPUT CLIENT SOURCE_ID ROUTE FRAMES\n"
		<< "  peer mix INPUT OUTPUT\n"
		<< "  peer render INPUT OUTPUT\n";
}

} // namespace

int main(int argc, char** argv)
{
	try {
		if (argc < 2) {
			usage();
			return 64;
		}
		const std::string command = argv[1];
		if (command == "generate" && argc == 7) {
			return generateClientTrace(argv[2], argv[3],
				static_cast<std::uint32_t>(std::stoul(argv[4])), argv[5], std::stoi(argv[6]));
		}
		if (command == "mix" && argc == 4) {
			return mixServerTrace(argv[2], argv[3]);
		}
		if (command == "render" && argc == 4) {
			return renderClientTrace(argv[2], argv[3]);
		}
		usage();
		return 64;
	}
	catch (const std::exception& error) {
		std::cerr << JAMMERNETZ_COMPAT_VERSION << " compatibility peer failed: "
			<< error.what() << '\n';
		return 1;
	}
}
