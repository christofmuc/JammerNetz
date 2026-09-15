/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ControlTransport.h"
#include "Client.h"

#include <gtest/gtest.h>

TEST(ControlTransportTest, IncomingQueueIsBoundedAndOverflowIsObservable)
{
	DatagramSocket socket;
	Client client(socket);
	ControlTransport transport(client);
	JammerNetzControlEnvelopeData envelope;
	envelope.messageId = 1;
	envelope.topic = JammerNetzControlProtocol::PingTopic;

	for (std::size_t index = 0; index < 128; ++index) {
		envelope.messageId = static_cast<uint64_t>(index + 1);
		EXPECT_TRUE(transport.enqueueIncoming(envelope));
	}
	EXPECT_FALSE(transport.enqueueIncoming(envelope));
	EXPECT_EQ(transport.stats().incomingOverflows, 1u);
}

TEST(ControlTransportTest, DoesNotSendApplicationMessagesBeforeNegotiation)
{
	DatagramSocket socket;
	Client client(socket);
	ControlTransport transport(client);
	EXPECT_FALSE(transport.send("jn.test.message.v1", {{ "value", 1 }}));
}
