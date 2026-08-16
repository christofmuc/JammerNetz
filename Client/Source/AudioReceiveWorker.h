/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "BoundedSpscQueue.h"
#include "IncludeFFMeters.h"
#include "JammerNetzSession.h"
#include "PacketStreamQueue.h"
#include "RealtimeAudioFrames.h"

class AudioReceiveWorker final : private juce::Thread {
public:
	explicit AudioReceiveWorker(JammerNetzSession& session);
	~AudioReceiveWorker() override;

	void start();
	void shutdown();
	void enqueue(std::shared_ptr<JammerNetzAudioData> packet);
	bool tryPop(RemoteAudioFrame& frame);
	int readyFrames() const noexcept;

	void setPlayoutRange(uint64_t minimumFrames, uint64_t maximumFrames) noexcept;
	void requestRebuffer() noexcept;
	uint64_t requestReset() noexcept;
	uint64_t currentGeneration() const noexcept;
	std::optional<float> takeServerBpmUpdate() noexcept;

	uint64_t discardedFrames() const noexcept;
	uint64_t outputQueueOverruns() const noexcept;
	std::string qualityStatement() const;
	FFAU::LevelMeterSource* meterSource() noexcept;

private:
	void run() override;
	void applyResetIfRequested();
	void drainInbound();
	bool prepareOneFrame();
	void updateSessionMeter();

	// Network bursts are dropped at 512 packets. Prepared PCM waits in a
	// separate 256-frame queue; the worker pauses instead of blocking audio.
	static constexpr int inputCapacity = 512;
	static constexpr int outputCapacity = 256;
	JammerNetzSession& session_;
	BoundedSpscQueue<std::shared_ptr<JammerNetzAudioData>> inboundQueue_ { inputCapacity };
	PacketStreamQueue packetQueue_ { "server" };
	BoundedSpscQueue<RemoteAudioFrame> outputQueue_ { outputCapacity };
	FFAU::LevelMeterSource sessionMeterSource_;
	std::atomic<uint64_t> minimumFrames_ { CLIENT_PLAYOUT_JITTER_BUFFER };
	std::atomic<uint64_t> maximumFrames_ { CLIENT_PLAYOUT_MAX_BUFFER };
	std::atomic<bool> rebufferRequested_ { false };
	std::atomic<uint64_t> requestedGeneration_ { 0 };
	std::atomic<uint64_t> activeGeneration_ { 0 };
	std::atomic<bool> streamStarted_ { false };
	std::atomic<float> latestServerBpm_ { 0.0f };
	std::atomic<bool> serverBpmPending_ { false };
	std::atomic<uint64_t> discarded_ { 0 };
	std::atomic<uint64_t> inboundOverruns_ { 0 };
	std::atomic<uint64_t> outputOverruns_ { 0 };
	bool recoveringFromOverrun_ { false };
};
