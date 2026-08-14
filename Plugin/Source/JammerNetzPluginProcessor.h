/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JammerNetzAudioEngine.h"

#include <atomic>

struct JammerNetzPluginConfiguration {
	juce::String serverName;
	int serverPort { 7777 };
	juce::String username { "Musician" };
	float sendGain { 1.0f };
	float remoteGain { 1.0f };
	float dryGain { 1.0f };
	uint64_t minimumJitterFrames { CLIENT_PLAYOUT_JITTER_BUFFER };
	uint64_t maximumJitterFrames { CLIENT_PLAYOUT_MAX_BUFFER };
	bool useLocalhost { false };
	bool useFEC { false };
	bool localPassthrough { true };
};

class SingleActiveSessionLease {
public:
	~SingleActiveSessionLease() { release(); }
	bool acquire(void* owner) noexcept;
	void release() noexcept;
	bool ownsLease() const noexcept { return owner_ != nullptr; }

private:
	static std::atomic<void*> activeOwner_;
	void* owner_ { nullptr };
};

class JammerNetzPluginProcessor final : public juce::AudioProcessor {
public:
	JammerNetzPluginProcessor();
	~JammerNetzPluginProcessor() override;

	void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
	void releaseResources() override;
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
	void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
	void setNonRealtime(bool shouldUseNonRealtimeMode) noexcept override;

	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
	const juce::String getName() const override { return "JammerNetz"; }
	bool acceptsMidi() const override { return false; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 0.0; }
	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String&) override {}

	bool hasEditor() const override { return true; }
	juce::AudioProcessorEditor* createEditor() override;

	void getStateInformation(juce::MemoryBlock& destinationData) override;
	void setStateInformation(const void* data, int sizeInBytes) override;

	bool connectSession();
	void disconnectSession() noexcept;
	bool isSessionActive() const noexcept { return connected_.load(std::memory_order_acquire); }
	bool isReceivingAudio() const;
	juce::String statusText() const;

	JammerNetzPluginConfiguration configuration() const;
	void setConfiguration(const JammerNetzPluginConfiguration& configuration);
	juce::String machineKeyPath() const;
	void setMachineKeyPath(const juce::String& path);

	double inputLevel(int channel);
	double remoteLevel(int channel);

private:
	static constexpr uint32_t closingMask = uint32_t { 1 } << 31;
	static constexpr uint32_t processCountMask = ~closingMask;

	bool enterProcess() noexcept;
	void leaveProcess() noexcept;
	void configureEngine(const JammerNetzPluginConfiguration& configuration);
	JammerNetzSessionConfiguration makeSessionConfiguration(const JammerNetzPluginConfiguration& configuration) const;
	void setError(const juce::String& error);
	void clearError();
	static JammerNetzPluginConfiguration sanitise(JammerNetzPluginConfiguration configuration);

	mutable juce::CriticalSection configurationLock_;
	JammerNetzPluginConfiguration configuration_;
	mutable juce::CriticalSection statusLock_;
	juce::String error_;

	JammerNetzSession session_;
	JammerNetzAudioEngine engine_;
	juce::AudioBuffer<float> remoteScratch_ { 2, JAMMERNETZ_MAX_CALLBACK_SAMPLES };
	SingleActiveSessionLease instanceLease_;
	std::atomic<bool> connected_ { false };
	std::atomic<uint32_t> processGate_ { 0 };
	std::atomic<double> preparedSampleRate_ { 0.0 };
	std::atomic<float> dryGain_ { 1.0f };
	std::atomic<bool> localPassthrough_ { true };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JammerNetzPluginProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
