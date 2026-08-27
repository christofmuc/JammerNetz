/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"
#include "Encryption.h"
#include "SessionControlHub.h"
#include "SharedServerTypes.h"

#include "tbb/concurrent_queue.h"

#include <atomic>
#include <map>
#include <memory>

struct SessionControlThreadStats {
	uint64_t inboundQueueOverflows { 0 };
	uint64_t outboundQueueOverflows { 0 };
	uint64_t outboundSocketDrops { 0 };
	uint64_t malformedEndpoints { 0 };
};

class SessionControlThread : public Thread {
public:
	SessionControlThread(DatagramSocket& socket, CriticalSection& socketWriteLock,
		TControlIncomingQueue& incomingQueue, void* keydata, int keysize);

	void run() override;
	[[nodiscard]] SessionControlThreadStats stats() const noexcept;

private:
	struct EndpointAddress {
		String address;
		int port { 0 };
	};

	void queueResult(SessionControlResult result);
	void drainOutgoing();
	bool send(const RoutedControlEnvelope& routed);

	DatagramSocket& socket_;
	CriticalSection& socketWriteLock_;
	TControlIncomingQueue& incomingQueue_;
	tbb::concurrent_bounded_queue<RoutedControlEnvelope> outgoingQueue_;
	SessionControlHub hub_;
	std::map<std::string, EndpointAddress> endpointAddresses_;
	std::unique_ptr<BlowFish> blowFish_;
	uint8 writeBuffer_[MAXFRAMESIZE] {};
	std::atomic<uint64_t> outboundQueueOverflows_ { 0 };
	std::atomic<uint64_t> outboundSocketDrops_ { 0 };
	std::atomic<uint64_t> malformedEndpoints_ { 0 };
};
