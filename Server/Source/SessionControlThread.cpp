/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SessionControlThread.h"

#include "Encryption.h"
#include "ServerLogger.h"
#include "XPlatformUtils.h"

#include <utility>

namespace {
uint64_t createSessionEpoch()
{
	const auto randomValue = static_cast<uint64_t>(Random::getSystemRandom().nextInt64());
	return randomValue == 0 ? 1 : randomValue;
}
}

SessionControlThread::SessionControlThread(DatagramSocket& socket,
	CriticalSection& socketWriteLock, TControlIncomingQueue& incomingQueue,
	void* keydata, const int keysize)
	: Thread("SessionControlThread")
	, socket_(socket)
	, socketWriteLock_(socketWriteLock)
	, incomingQueue_(incomingQueue)
	, hub_(createSessionEpoch())
{
	outgoingQueue_.set_capacity(128);
	if (keydata != nullptr) {
		blowFish_ = std::make_unique<BlowFish>(keydata, keysize);
	}
}

void SessionControlThread::run()
{
	while (!currentThreadShouldExit()) {
		ControlIncomingPackage incoming;
		incomingQueue_.pop(incoming);
		if (currentThreadShouldExit() || incoming.sourceEndpoint.empty()) {
			return;
		}
		endpointAddresses_[incoming.sourceEndpoint] = { incoming.sourceAddress, incoming.sourcePort };
		queueResult(hub_.process(incoming.sourceEndpoint, incoming.envelope));
		drainOutgoing();
	}
}

SessionControlThreadStats SessionControlThread::stats() const noexcept
{
	SessionControlThreadStats result;
	result.outboundQueueOverflows = outboundQueueOverflows_.load(std::memory_order_relaxed);
	result.outboundSocketDrops = outboundSocketDrops_.load(std::memory_order_relaxed);
	result.malformedEndpoints = malformedEndpoints_.load(std::memory_order_relaxed);
	return result;
}

void SessionControlThread::queueResult(SessionControlResult result)
{
	for (const auto& message : result.serverMessages) {
		ServerLogger::printServerStatus("Control message accepted for server topic " + message.topic);
	}
	for (auto& outbound : result.outbound) {
		if (!outgoingQueue_.try_push(std::move(outbound))) {
			outboundQueueOverflows_.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

void SessionControlThread::drainOutgoing()
{
	RoutedControlEnvelope routed;
	while (outgoingQueue_.try_pop(routed)) {
		if (!send(routed)) {
			outboundSocketDrops_.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

bool SessionControlThread::send(const RoutedControlEnvelope& routed)
{
	const auto address = endpointAddresses_.find(routed.targetEndpoint);
	if (address == endpointAddresses_.end() || address->second.port <= 0) {
		malformedEndpoints_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	JammerNetzControlEnvelopeMessage message(routed.envelope);
	size_t plaintextBytes = 0;
	message.serialize(writeBuffer_, plaintextBytes);
	if (!sizet_is_safe_as_int(plaintextBytes)) {
		return false;
	}
	int wireBytes = static_cast<int>(plaintextBytes);
	if (blowFish_) {
		wireBytes = blowFish_->encrypt(writeBuffer_, plaintextBytes, MAXFRAMESIZE);
		if (wireBytes <= 0) {
			return false;
		}
	}

	if (!socketWriteLock_.tryEnter()) {
		return false;
	}
	const auto bytesWritten = socket_.write(address->second.address, address->second.port,
		writeBuffer_, wireBytes);
	socketWriteLock_.exit();
	return bytesWritten == wireBytes;
}
