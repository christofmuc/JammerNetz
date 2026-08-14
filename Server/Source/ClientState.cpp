/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ClientState.h"

#include <stack>
#include <utility>

ClientState::ClientState(std::string clientName) : clientName_(std::move(clientName)) {
}

ClientPushResult ClientState::push(std::shared_ptr<JammerNetzAudioData> packet,
	std::size_t initialPrefillCount, TimePoint /*now*/) {
	std::lock_guard<std::mutex> lock(mutex_);

	ClientConnectionTransition transition = ClientConnectionTransition::None;
	const bool isInitialConnection = !hasConnected_;
	if (!queue_) {
		queue_ = std::make_shared<PacketStreamQueue>(clientName_);
		transition = isInitialConnection ? ClientConnectionTransition::InitialConnection
			: ClientConnectionTransition::Reconnection;
	}
	else if (state_ == ClientConnectionState::Disconnecting) {
		transition = ClientConnectionTransition::GraceRecovery;
	}

	state_ = ClientConnectionState::Connected;
	hasConnected_ = true;
	++activityGeneration_;

	// Preserve the existing behavior: only the first connection is prefixed with
	// padding. A reconnect starts with the first real packet and a fresh queue.
	if (isInitialConnection) {
		auto lastInserted = packet;
		std::stack<std::shared_ptr<JammerNetzAudioData>> reverse;
		for (std::size_t i = 0; i < initialPrefillCount; ++i) {
			lastInserted = lastInserted->createPrePaddingPackage();
			reverse.push(lastInserted);
		}
		while (!reverse.empty()) {
			queue_->push(reverse.top());
			reverse.pop();
		}
	}

	return {queue_->push(std::move(packet)), transition};
}

bool ClientState::tryPop(std::shared_ptr<JammerNetzAudioData> &packet, bool &isFillIn,
	std::uint64_t &observedActivityGeneration) {
	std::lock_guard<std::mutex> lock(mutex_);
	observedActivityGeneration = activityGeneration_;
	if (state_ == ClientConnectionState::Disconnected || !queue_) {
		return false;
	}
	return queue_->try_pop(packet, isFillIn);
}

ClientQueueSnapshot ClientState::snapshot() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return {state_, state_ == ClientConnectionState::Disconnected || !queue_ ? 0 : queue_->size(),
		activityGeneration_};
}

bool ClientState::qualityInfo(JammerNetzStreamQualityInfo &qualityInfo) const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (state_ == ClientConnectionState::Disconnected || !queue_) {
		return false;
	}
	qualityInfo = queue_->qualityInfoPackage();
	return true;
}

bool ClientState::markUnderrun(std::uint64_t observedActivityGeneration, TimePoint now) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (state_ != ClientConnectionState::Connected || activityGeneration_ != observedActivityGeneration) {
		return false;
	}
	state_ = ClientConnectionState::Disconnecting;
	disconnectDeadline_ = now + DisconnectGracePeriod;
	return true;
}

bool ClientState::disconnectIfGraceExpired(TimePoint now) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (state_ != ClientConnectionState::Disconnecting || now < disconnectDeadline_) {
		return false;
	}
	state_ = ClientConnectionState::Disconnected;
	queue_.reset();
	return true;
}
