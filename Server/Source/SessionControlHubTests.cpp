/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "SessionControlHub.h"

#include <gtest/gtest.h>

namespace {
JammerNetzControlEnvelopeData hello(const uint64_t messageId, const char* instance)
{
	JammerNetzControlEnvelopeData result;
	result.messageId = messageId;
	result.delivery = JammerNetzControlDelivery::Acknowledged;
	result.topic = JammerNetzControlProtocol::HelloTopic;
	result.payload = {{ "instance", instance }, { "versions", { 1 } }};
	return result;
}

uint32_t registerParticipant(SessionControlHub& hub, const char* endpoint,
	const char* instance)
{
	const auto result = hub.process(endpoint, hello(1, instance));
	EXPECT_TRUE(result.accepted);
	EXPECT_FALSE(result.outbound.empty());
	return result.outbound.front().envelope.payload["participant_id"].get<uint32_t>();
}

JammerNetzControlEnvelopeData message(const uint64_t epoch, const uint64_t id,
	const char* topic)
{
	JammerNetzControlEnvelopeData result;
	result.sessionEpoch = epoch;
	result.messageId = id;
	result.topic = topic;
	result.payload = {{ "value", 42 }};
	return result;
}
}

TEST(SessionControlHubTest, RegistersAndReturnsSessionScopedIdentity)
{
	SessionControlHub hub(1234);
	const auto result = hub.process("client-a", hello(1, "instance-a"));

	ASSERT_TRUE(result.accepted);
	ASSERT_EQ(result.outbound.size(), 1u);
	const auto& welcome = result.outbound.front().envelope;
	EXPECT_EQ(welcome.topic, JammerNetzControlProtocol::WelcomeTopic);
	EXPECT_EQ(welcome.sessionEpoch, 1234u);
	EXPECT_EQ(welcome.acknowledgementFor, 1u);
	EXPECT_GT(welcome.payload["participant_id"].get<uint32_t>(), 0u);
	EXPECT_EQ(hub.participantCount(), 1u);
}

TEST(SessionControlHubTest, RoutesUnicastAndStampsTrustedSenderIdentity)
{
	SessionControlHub hub(55);
	const auto sourceId = registerParticipant(hub, "client-a", "instance-a");
	const auto targetId = registerParticipant(hub, "client-b", "instance-b");
	auto control = message(55, 2, "jn.test.unicast.v1");
	control.route = JammerNetzControlRoute::Unicast;
	control.targetId = targetId;
	control.senderId = 999;
	control.delivery = JammerNetzControlDelivery::Acknowledged;

	const auto result = hub.process("client-a", control);
	ASSERT_TRUE(result.accepted);
	ASSERT_EQ(result.outbound.size(), 2u);
	EXPECT_EQ(result.outbound.front().targetEndpoint, "client-b");
	EXPECT_EQ(result.outbound.front().envelope.senderId, sourceId);
	EXPECT_EQ(result.outbound.front().envelope.targetId, targetId);
	EXPECT_EQ(result.outbound.back().envelope.topic,
		JammerNetzControlProtocol::AcknowledgementTopic);
}

TEST(SessionControlHubTest, BroadcastExcludesSenderUnlessRequested)
{
	SessionControlHub hub(77);
	registerParticipant(hub, "client-a", "instance-a");
	registerParticipant(hub, "client-b", "instance-b");
	registerParticipant(hub, "client-c", "instance-c");
	auto control = message(77, 2, "jn.test.broadcast.v1");
	control.route = JammerNetzControlRoute::Broadcast;

	auto result = hub.process("client-a", control);
	ASSERT_EQ(result.outbound.size(), 2u);
	for (const auto& outbound : result.outbound) {
		EXPECT_NE(outbound.targetEndpoint, "client-a");
	}

	control.messageId = 3;
	control.includeSender = true;
	result = hub.process("client-a", control);
	EXPECT_EQ(result.outbound.size(), 3u);
}

TEST(SessionControlHubTest, DropsDuplicateAndStaleSessionMessages)
{
	SessionControlHub hub(99);
	registerParticipant(hub, "client-a", "instance-a");
	auto control = message(99, 2, "jn.test.server.v1");
	control.delivery = JammerNetzControlDelivery::Acknowledged;
	EXPECT_TRUE(hub.process("client-a", control).accepted);

	const auto duplicate = hub.process("client-a", control);
	EXPECT_FALSE(duplicate.accepted);
	ASSERT_EQ(duplicate.outbound.size(), 1u);
	EXPECT_EQ(duplicate.outbound.front().envelope.payload["status"], "duplicate");

	control.messageId = 3;
	control.sessionEpoch = 98;
	const auto stale = hub.process("client-a", control);
	EXPECT_FALSE(stale.accepted);
	ASSERT_EQ(stale.outbound.size(), 1u);
	EXPECT_EQ(stale.outbound.front().envelope.payload["reason"], "stale_session");
}

TEST(SessionControlHubTest, ReplaysRetainedBroadcastAfterJoin)
{
	SessionControlHub hub(101);
	registerParticipant(hub, "client-a", "instance-a");
	registerParticipant(hub, "client-b", "instance-b");
	auto retained = message(101, 2, "jn.test.retained.v1");
	retained.route = JammerNetzControlRoute::Broadcast;
	retained.delivery = JammerNetzControlDelivery::Retained;
	ASSERT_TRUE(hub.process("client-a", retained).accepted);

	const auto joined = hub.process("client-c", hello(1, "instance-c"));
	ASSERT_EQ(joined.outbound.size(), 2u);
	EXPECT_EQ(joined.outbound[1].targetEndpoint, "client-c");
	EXPECT_EQ(joined.outbound[1].envelope.topic, "jn.test.retained.v1");
}

TEST(SessionControlHubTest, NewClientInstanceRetiresPreviousIdentity)
{
	SessionControlHub hub(202);
	const auto oldId = registerParticipant(hub, "same-endpoint", "first-instance");
	const auto newRegistration = hub.process("same-endpoint", hello(1, "second-instance"));
	const auto newId = newRegistration.outbound.front().envelope.payload["participant_id"].get<uint32_t>();
	EXPECT_NE(oldId, newId);
	EXPECT_EQ(hub.participantCount(), 1u);

	registerParticipant(hub, "controller", "controller-instance");
	auto control = message(202, 2, "jn.test.unicast.v1");
	control.route = JammerNetzControlRoute::Unicast;
	control.targetId = oldId;
	const auto result = hub.process("controller", control);
	EXPECT_FALSE(result.accepted);
	ASSERT_EQ(result.outbound.size(), 1u);
	EXPECT_EQ(result.outbound.front().envelope.payload["reason"], "target_not_found");
}

TEST(SessionControlHubTest, AuthorizationPolicyCanDenyPeerAndGlobalOperations)
{
	SessionControlHub hub(303, [](const uint32_t, const JammerNetzControlEnvelopeData& envelope) {
		return envelope.route == JammerNetzControlRoute::Server;
	});
	registerParticipant(hub, "client-a", "instance-a");
	const auto targetId = registerParticipant(hub, "client-b", "instance-b");
	auto control = message(303, 2, "jn.test.denied.v1");
	control.route = JammerNetzControlRoute::Unicast;
	control.targetId = targetId;

	const auto result = hub.process("client-a", control);
	EXPECT_FALSE(result.accepted);
	ASSERT_EQ(result.outbound.size(), 1u);
	EXPECT_EQ(result.outbound.front().targetEndpoint, "client-a");
	EXPECT_EQ(result.outbound.front().envelope.payload["reason"], "unauthorized");
	EXPECT_EQ(hub.stats().unauthorized, 1u);
}

TEST(SessionControlHubTest, ReconnectOnANewEndpointRetiresOldIdentityAndState)
{
	SessionControlHub hub(404);
	const auto oldId = registerParticipant(hub, "client-old", "same-instance");
	auto retained = message(404, 2, "jn.test.retained.v1");
	retained.route = JammerNetzControlRoute::Broadcast;
	retained.delivery = JammerNetzControlDelivery::Retained;
	ASSERT_TRUE(hub.process("client-old", retained).accepted);

	const auto newId = registerParticipant(hub, "client-new", "same-instance");
	EXPECT_NE(oldId, newId);
	EXPECT_EQ(hub.participantCount(), 1u);
	const auto observer = hub.process("observer", hello(1, "observer-instance"));
	ASSERT_EQ(observer.outbound.size(), 1u);
	EXPECT_EQ(observer.outbound.front().envelope.topic, JammerNetzControlProtocol::WelcomeTopic);
}

TEST(SessionControlHubTest, PeerCannotForgeServerTopicsOrAcknowledgements)
{
	SessionControlHub hub(505);
	registerParticipant(hub, "client-a", "instance-a");
	const auto targetId = registerParticipant(hub, "client-b", "instance-b");
	auto forgedWelcome = message(505, 2, JammerNetzControlProtocol::WelcomeTopic);
	forgedWelcome.route = JammerNetzControlRoute::Unicast;
	forgedWelcome.targetId = targetId;
	forgedWelcome.acknowledgementFor = 123;

	const auto rejected = hub.process("client-a", forgedWelcome);
	EXPECT_FALSE(rejected.accepted);
	ASSERT_EQ(rejected.outbound.size(), 1u);
	EXPECT_EQ(rejected.outbound.front().targetEndpoint, "client-a");
	EXPECT_EQ(rejected.outbound.front().envelope.payload["reason"], "reserved_topic");

	auto applicationMessage = message(505, 3, "jn.test.safe.v1");
	applicationMessage.route = JammerNetzControlRoute::Unicast;
	applicationMessage.targetId = targetId;
	applicationMessage.acknowledgementFor = 456;
	const auto routed = hub.process("client-a", applicationMessage);
	ASSERT_TRUE(routed.accepted);
	ASSERT_EQ(routed.outbound.size(), 1u);
	EXPECT_EQ(routed.outbound.front().envelope.acknowledgementFor, 0u);
}

TEST(SessionControlHubTest, ServerCanPublishTypedUnicastAndBroadcastResponses)
{
	SessionControlHub hub(606);
	const auto participantA = registerParticipant(hub, "client-a", "instance-a");
	registerParticipant(hub, "client-b", "instance-b");

	const auto unicast = hub.publish("jn.test.server-response.v1", {{ "ok", true }},
		JammerNetzControlRoute::Unicast, participantA,
		JammerNetzControlDelivery::Acknowledged);
	ASSERT_TRUE(unicast.accepted);
	ASSERT_EQ(unicast.outbound.size(), 1u);
	EXPECT_EQ(unicast.outbound.front().targetEndpoint, "client-a");
	EXPECT_EQ(unicast.outbound.front().envelope.senderId, 0u);

	const auto broadcast = hub.publish("jn.test.server-state.v1", {{ "revision", 7 }},
		JammerNetzControlRoute::Broadcast, 0,
		JammerNetzControlDelivery::Retained);
	ASSERT_TRUE(broadcast.accepted);
	EXPECT_EQ(broadcast.outbound.size(), 2u);
	for (const auto& routed : broadcast.outbound) {
		EXPECT_EQ(routed.envelope.route, JammerNetzControlRoute::Broadcast);
		EXPECT_EQ(routed.envelope.sessionEpoch, 606u);
	}

	const auto joined = hub.process("client-c", hello(1, "instance-c"));
	ASSERT_EQ(joined.outbound.size(), 2u);
	EXPECT_EQ(joined.outbound.back().envelope.topic, "jn.test.server-state.v1");
}
