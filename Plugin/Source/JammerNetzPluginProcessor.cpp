/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzPluginProcessor.h"

#include "Encryption.h"
#include "JammerNetzPluginEditor.h"
#include "Settings.h"

#include <array>
#include <cmath>

namespace {

constexpr auto stateType = "JammerNetzPluginState";
constexpr auto machineKeyPathSetting = "pluginCryptoKeyPath";

juce::String limitedChannelName(juce::String name)
{
	return name.substring(0, 20);
}

} // namespace

std::atomic<void*> SingleActiveSessionLease::activeOwner_ { nullptr };

bool SingleActiveSessionLease::acquire(void* owner) noexcept
{
	if (owner_ == owner) {
		return true;
	}
	void* expected = nullptr;
	if (!owner || !activeOwner_.compare_exchange_strong(expected, owner, std::memory_order_acq_rel)) {
		return false;
	}
	owner_ = owner;
	return true;
}

void SingleActiveSessionLease::release() noexcept
{
	if (!owner_) {
		return;
	}
	void* expected = owner_;
	activeOwner_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
	owner_ = nullptr;
}

JammerNetzPluginProcessor::JammerNetzPluginProcessor()
	: AudioProcessor(BusesProperties()
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
		.withOutput("Output", juce::AudioChannelSet::stereo(), true))
	, engine_(session_, juce::File())
{
}

JammerNetzPluginProcessor::~JammerNetzPluginProcessor()
{
	disconnectSession();
}

void JammerNetzPluginProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
	const auto previousSampleRate = preparedSampleRate_.exchange(sampleRate, std::memory_order_acq_rel);
	if (isSessionActive() && std::abs(previousSampleRate - sampleRate) > 0.5) {
		disconnectSession();
	}
	engine_.prepare(sampleRate, std::min(maximumExpectedSamplesPerBlock, JAMMERNETZ_MAX_CALLBACK_SAMPLES));
	if (std::abs(sampleRate - static_cast<double>(SAMPLE_RATE)) > 0.5) {
		setError("JammerNetz currently requires a 48 kHz host sample rate", ErrorSource::sampleRate);
	} else {
		clearError(ErrorSource::sampleRate);
	}
}

void JammerNetzPluginProcessor::releaseResources()
{
	disconnectSession();
	engine_.release();
}

bool JammerNetzPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
		&& layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

bool JammerNetzPluginProcessor::enterProcess() noexcept
{
	if (!connected_.load(std::memory_order_acquire)) {
		return false;
	}
	auto state = processGate_.load(std::memory_order_acquire);
	while ((state & closingMask) == 0 && (state & processCountMask) != processCountMask) {
		if (processGate_.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
			if (connected_.load(std::memory_order_acquire)) {
				return true;
			}
			leaveProcess();
			return false;
		}
	}
	return false;
}

void JammerNetzPluginProcessor::leaveProcess() noexcept
{
	processGate_.fetch_sub(1, std::memory_order_release);
}

void JammerNetzPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	midiMessages.clear();
	if (isNonRealtime() || buffer.getNumChannels() < 2 || !enterProcess()) {
		return;
	}

	struct ProcessExit {
		JammerNetzPluginProcessor& processor;
		~ProcessExit() { processor.leaveProcess(); }
	} processExit { *this };
	// A resume edge invalidates everything received while the host bypassed us.
	// Normal processing below then waits for a fresh minimum jitter buffer.
	engine_.setBypassed(false);

	if (auto* hostPlayHead = getPlayHead()) {
		if (const auto position = hostPlayHead->getPosition()) {
			if (const auto bpm = position->getBpm()) {
				engine_.setClientBpm(static_cast<float>(*bpm));
			}
		}
	}

	const auto dryGain = dryGain_.load(std::memory_order_relaxed);
	const auto localPassthrough = localPassthrough_.load(std::memory_order_relaxed);
	const int samples = buffer.getNumSamples();
	for (int offset = 0; offset < samples; offset += JAMMERNETZ_MAX_CALLBACK_SAMPLES) {
		const int block = std::min(JAMMERNETZ_MAX_CALLBACK_SAMPLES, samples - offset);
		std::array<const float*, 2> inputs {
			buffer.getReadPointer(0, offset), buffer.getReadPointer(1, offset)
		};
		std::array<float*, 2> remote {
			remoteScratch_.getWritePointer(0), remoteScratch_.getWritePointer(1)
		};
		engine_.process(inputs.data(), 2, remote.data(), 2, block);

		if (!session_.isReceivingData()) {
			continue;
		}
		if (localPassthrough) {
			buffer.applyGain(0, offset, block, dryGain);
			buffer.applyGain(1, offset, block, dryGain);
		} else {
			buffer.clear(0, offset, block);
			buffer.clear(1, offset, block);
		}
		buffer.addFrom(0, offset, remoteScratch_, 0, 0, block);
		buffer.addFrom(1, offset, remoteScratch_, 1, 0, block);
	}
}

void JammerNetzPluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ignoreUnused(buffer);
	midiMessages.clear();
	// Do not run the normal engine path here: bypass must not transmit, record,
	// schedule MIDI clock, meter, or alter the host's dry buffer. Invalidating on
	// entry and again on resume keeps the connected receive stream current.
	engine_.setBypassed(true);
}

void JammerNetzPluginProcessor::setNonRealtime(bool shouldUseNonRealtimeMode) noexcept
{
	AudioProcessor::setNonRealtime(shouldUseNonRealtimeMode);
	if (shouldUseNonRealtimeMode) {
		disconnectSession();
	}
}

JammerNetzPluginConfiguration JammerNetzPluginProcessor::sanitise(JammerNetzPluginConfiguration configuration)
{
	configuration.serverName = configuration.serverName.trim();
	configuration.username = configuration.username.trim().substring(0, 20);
	configuration.serverPort = juce::jlimit(1, 65535, configuration.serverPort);
	configuration.sendGain = juce::jlimit(0.0f, 2.0f, configuration.sendGain);
	configuration.remoteGain = juce::jlimit(0.0f, 2.0f, configuration.remoteGain);
	configuration.dryGain = juce::jlimit(0.0f, 2.0f, configuration.dryGain);
	configuration.minimumJitterFrames = std::min<uint64_t>(256, configuration.minimumJitterFrames);
	configuration.maximumJitterFrames = std::min<uint64_t>(256,
		std::max(configuration.minimumJitterFrames, configuration.maximumJitterFrames));
	return configuration;
}

JammerNetzPluginConfiguration JammerNetzPluginProcessor::configuration() const
{
	const juce::ScopedLock lock(configurationLock_);
	return configuration_;
}

void JammerNetzPluginProcessor::setConfiguration(const JammerNetzPluginConfiguration& configuration)
{
	if (isSessionActive()) {
		disconnectSession();
	}
	const auto clean = sanitise(configuration);
	{
		const juce::ScopedLock lock(configurationLock_);
		configuration_ = clean;
	}
	dryGain_.store(clean.dryGain, std::memory_order_relaxed);
	localPassthrough_.store(clean.localPassthrough, std::memory_order_relaxed);
}

void JammerNetzPluginProcessor::configureEngine(const JammerNetzPluginConfiguration& config)
{
	JammerNetzChannelSetup setup(config.localPassthrough);
	JammerNetzSingleChannelSetup left(JammerNetzChannelTarget::Left);
	left.volume = config.sendGain;
	left.name = limitedChannelName(config.username + " L").toStdString();
	JammerNetzSingleChannelSetup right(JammerNetzChannelTarget::Right);
	right.volume = config.sendGain;
	right.name = limitedChannelName(config.username + " R").toStdString();
	setup.channels = { left, right };
	engine_.setChannelSetup(setup);
	engine_.setLocalMonitoring(false);
	engine_.setMonitorBalance(1.0);
	engine_.setMasterVolume(config.remoteGain);
	engine_.setPlayoutBufferRange(config.minimumJitterFrames, config.maximumJitterFrames);
	engine_.newServer();
}

JammerNetzSessionConfiguration JammerNetzPluginProcessor::makeSessionConfiguration(const JammerNetzPluginConfiguration& config) const
{
	JammerNetzSessionConfiguration sessionConfig;
	sessionConfig.serverName = config.serverName;
	sessionConfig.serverPort = config.serverPort;
	sessionConfig.useLocalhost = config.useLocalhost;
	sessionConfig.useFEC = config.useFEC;
	const auto keyPath = machineKeyPath();
	if (keyPath.isNotEmpty()) {
		std::string keyError;
		sessionConfig.sessionKey = JammerNetzSecure::SessionKey::load(
			std::filesystem::path(keyPath.toStdString()), keyError);
	}
	return sessionConfig;
}

bool JammerNetzPluginProcessor::connectSession()
{
	if (isSessionActive()) {
		return true;
	}
	if (std::abs(preparedSampleRate_.load(std::memory_order_acquire) - static_cast<double>(SAMPLE_RATE)) > 0.5) {
		setError("Set the host project to 48 kHz before connecting", ErrorSource::sampleRate);
		return false;
	}
	const auto config = configuration();
	if (!config.useLocalhost && config.serverName.isEmpty()) {
		setError("Enter a JammerNetz server before connecting");
		return false;
	}
	if (!instanceLease_.acquire(this)) {
		setError("Another JammerNetz plug-in instance is already active in this host");
		return false;
	}
	const auto sessionConfiguration = makeSessionConfiguration(config);
	if (!sessionConfiguration.sessionKey) {
		instanceLease_.release();
		setError("Select a valid JammerNetz session key before connecting");
		return false;
	}

	processGate_.store(0, std::memory_order_release);
	configureEngine(config);
	engine_.start(false);
	const bool started = session_.start(
		[this](std::shared_ptr<JammerNetzAudioData> audio) { engine_.enqueueRemoteAudio(std::move(audio)); },
		sessionConfiguration);
	if (!started) {
		engine_.shutdown();
		instanceLease_.release();
		setError(session_.startupError().isNotEmpty() ? session_.startupError() : "Could not start the JammerNetz session");
		return false;
	}
	connected_.store(true, std::memory_order_release);
	clearError();
	return true;
}

void JammerNetzPluginProcessor::disconnectSession() noexcept
{
	connected_.store(false, std::memory_order_release);
	processGate_.fetch_or(closingMask, std::memory_order_acq_rel);
	while ((processGate_.load(std::memory_order_acquire) & processCountMask) != 0) {
		juce::Thread::sleep(1);
	}
	// The process gate above quiesces audio; stop the network receive producer
	// before the engine resets its worker queues.
	session_.shutdown();
	engine_.shutdown();
	processGate_.store(0, std::memory_order_release);
	instanceLease_.release();
}

bool JammerNetzPluginProcessor::isReceivingAudio() const
{
	return isSessionActive() && session_.isReceivingData();
}

void JammerNetzPluginProcessor::setError(const juce::String& error, ErrorSource source)
{
	const juce::ScopedLock lock(statusLock_);
	error_ = error;
	errorSource_ = error.isEmpty() ? ErrorSource::none : source;
}

void JammerNetzPluginProcessor::clearError()
{
	setError({});
}

void JammerNetzPluginProcessor::clearError(ErrorSource source)
{
	const juce::ScopedLock lock(statusLock_);
	if (errorSource_ == source) {
		error_.clear();
		errorSource_ = ErrorSource::none;
	}
}

juce::String JammerNetzPluginProcessor::statusText() const
{
	{
		const juce::ScopedLock lock(statusLock_);
		if (error_.isNotEmpty()) {
			return error_;
		}
	}
	if (!isSessionActive()) {
		return "Disconnected";
	}
	if (!session_.isReceivingData()) {
		return "Connected; waiting for server audio";
	}
	return "Receiving  |  RTT " + juce::String(session_.currentRTT(), 1) + " ms  |  "
		+ juce::String(engine_.currentReceptionQuality());
}

juce::String JammerNetzPluginProcessor::machineKeyPath() const
{
	return Settings::instance().get(machineKeyPathSetting);
}

void JammerNetzPluginProcessor::setMachineKeyPath(const juce::String& path)
{
	Settings::instance().set(machineKeyPathSetting, path.toStdString());
	Settings::instance().flush();
}

double JammerNetzPluginProcessor::inputLevel(int channel)
{
	auto* source = engine_.getMeterSource();
	return source && channel >= 0 ? source->getMaxLevel(channel) : 0.0;
}

double JammerNetzPluginProcessor::remoteLevel(int channel)
{
	auto* source = engine_.getOutputMeterSource();
	return source && channel >= 0 ? source->getMaxLevel(channel) : 0.0;
}

void JammerNetzPluginProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
	const auto config = configuration();
	juce::ValueTree state(stateType);
	state.setProperty("schemaVersion", 1, nullptr);
	state.setProperty("serverName", config.serverName, nullptr);
	state.setProperty("serverPort", config.serverPort, nullptr);
	state.setProperty("username", config.username, nullptr);
	state.setProperty("sendGain", config.sendGain, nullptr);
	state.setProperty("remoteGain", config.remoteGain, nullptr);
	state.setProperty("dryGain", config.dryGain, nullptr);
	state.setProperty("minimumJitterFrames", static_cast<juce::int64>(config.minimumJitterFrames), nullptr);
	state.setProperty("maximumJitterFrames", static_cast<juce::int64>(config.maximumJitterFrames), nullptr);
	state.setProperty("useLocalhost", config.useLocalhost, nullptr);
	state.setProperty("useFEC", config.useFEC, nullptr);
	state.setProperty("localPassthrough", config.localPassthrough, nullptr);
	if (const auto xml = state.createXml()) {
		copyXmlToBinary(*xml, destinationData);
	}
}

void JammerNetzPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
	const auto xml = getXmlFromBinary(data, sizeInBytes);
	if (!xml) {
		return;
	}
	const auto state = juce::ValueTree::fromXml(*xml);
	if (!state.isValid() || !state.hasType(stateType)) {
		return;
	}
	JammerNetzPluginConfiguration config;
	config.serverName = state.getProperty("serverName", config.serverName);
	config.serverPort = static_cast<int>(state.getProperty("serverPort", config.serverPort));
	config.username = state.getProperty("username", config.username);
	config.sendGain = static_cast<float>(state.getProperty("sendGain", config.sendGain));
	config.remoteGain = static_cast<float>(state.getProperty("remoteGain", config.remoteGain));
	config.dryGain = static_cast<float>(state.getProperty("dryGain", config.dryGain));
	config.minimumJitterFrames = static_cast<uint64_t>(static_cast<juce::int64>(state.getProperty("minimumJitterFrames", juce::var(static_cast<juce::int64>(config.minimumJitterFrames)))));
	config.maximumJitterFrames = static_cast<uint64_t>(static_cast<juce::int64>(state.getProperty("maximumJitterFrames", juce::var(static_cast<juce::int64>(config.maximumJitterFrames)))));
	config.useLocalhost = state.getProperty("useLocalhost", config.useLocalhost);
	config.useFEC = state.getProperty("useFEC", config.useFEC);
	config.localPassthrough = state.getProperty("localPassthrough", config.localPassthrough);
	setConfiguration(config);
}

juce::AudioProcessorEditor* JammerNetzPluginProcessor::createEditor()
{
	return new JammerNetzPluginEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new JammerNetzPluginProcessor();
}
