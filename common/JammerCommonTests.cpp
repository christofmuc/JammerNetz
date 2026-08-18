#include "JammerNetzPackage.h"
#include "PacketStreamQueue.h"

#include "BuffersConfig.h"

#include "gtest/gtest.h"

#include <cstring>

namespace {

std::shared_ptr<AudioBuffer<float>> makeAudioBuffer()
{
	auto buffer = std::make_shared<AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	for (int channel = 0; channel < buffer->getNumChannels(); ++channel) {
		auto samples = buffer->getWritePointer(channel);
		for (int i = 0; i < buffer->getNumSamples(); ++i) {
			samples[i] = static_cast<float>(i) / 3.0f;
		}
	}
	return buffer;
}

JammerNetzChannelSetup makeChannelSetup(std::string const &name = {})
{
	JammerNetzChannelSetup setup(false);
	JammerNetzSingleChannelSetup channel(JammerNetzChannelTarget::Left);
	channel.name = name;
	setup.channels.push_back(channel);
	return setup;
}

std::shared_ptr<JammerNetzAudioData> makeQueuePacket(std::uint64_t counter)
{
	return std::make_shared<JammerNetzAudioData>(
		counter, 0.0, makeChannelSetup(), SAMPLE_RATE, 120.0f, MidiSignal_None, makeAudioBuffer(), nullptr);
}

std::vector<uint8> makeLegacyPacket(JammerNetzChannelSetup const &sessionSetup, bool includeFec = false, bool omitSessionName = false)
{
	flatbuffers::FlatBufferBuilder fbb;

	std::vector<flatbuffers::Offset<JammerNetzPNPChannelSetup>> inputChannels;
	const auto inputName = fbb.CreateString("Legacy input");
	inputChannels.push_back(CreateJammerNetzPNPChannelSetup(fbb, JammerNetzChannelTarget::Left, 1.0f, 0.0f, 0.0f, 0.0f, inputName));
	const auto inputChannelVector = fbb.CreateVector(inputChannels);

	std::vector<flatbuffers::Offset<JammerNetzPNPChannelSetup>> sessionChannels;
	for (const auto &channel : sessionSetup.channels) {
		const auto name = omitSessionName ? flatbuffers::Offset<flatbuffers::String>() : fbb.CreateString(channel.name);
		sessionChannels.push_back(CreateJammerNetzPNPChannelSetup(fbb, channel.target, channel.volume, channel.mag, channel.rms, channel.pitch, name));
	}
	const auto sessionChannelVector = fbb.CreateVector(sessionChannels);

	std::vector<uint16> silence(SAMPLE_BUFFER_SIZE, 0);
	const auto sampleVector = fbb.CreateVector(silence);
	std::vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>> audioChannels;
	audioChannels.push_back(CreateJammerNetzPNPAudioSamples(fbb, sampleVector));
	const auto audioChannelVector = fbb.CreateVector(audioChannels);

	const auto makeAudioBlock = [&](uint64 messageCounter) {
		JammerNetzPNPAudioBlockBuilder audioBlock(fbb);
		audioBlock.add_timestamp(1234.0);
		audioBlock.add_messageCounter(messageCounter);
		audioBlock.add_numChannels(1);
		audioBlock.add_numberOfSamples(static_cast<uint16>(SAMPLE_BUFFER_SIZE));
		audioBlock.add_sampleRate(static_cast<uint16>(SAMPLE_RATE));
		audioBlock.add_channelSetup(inputChannelVector);
		audioBlock.add_channels(audioChannelVector);
		audioBlock.add_allChannels(sessionChannelVector);
		return audioBlock.Finish();
	};

	std::vector<flatbuffers::Offset<JammerNetzPNPAudioBlock>> audioBlocks { makeAudioBlock(7) };
	if (includeFec) {
		audioBlocks.push_back(makeAudioBlock(6));
	}
	const auto audioBlockVector = fbb.CreateVector(audioBlocks);
	JammerNetzPNPAudioDataBuilder audioData(fbb);
	audioData.add_audioBlocks(audioBlockVector);
	// Deliberately omit protocolVersion: rc4 packets predate the marker and read as version 0.
	fbb.Finish(audioData.Finish());

	std::vector<uint8> packet(sizeof(JammerNetzHeader) + fbb.GetSize());
	auto *header = reinterpret_cast<JammerNetzHeader *>(packet.data());
	header->magic0 = '1';
	header->magic1 = '2';
	header->magic2 = '3';
	header->messageType = JammerNetzMessage::AUDIODATA;
	std::memcpy(packet.data() + sizeof(JammerNetzHeader), fbb.GetBufferPointer(), fbb.GetSize());
	return packet;
}

const JammerNetzPNPAudioData *audioRoot(uint8 const *packet)
{
	return GetJammerNetzPNPAudioData(packet + sizeof(JammerNetzHeader));
}

}

TEST(TestSerialization, TestAudioData) {
	auto buffer = makeAudioBuffer();
	auto setup = makeChannelSetup();

	JammerNetzAudioData  message(0, 1234.0, setup, SAMPLE_RATE, 0.0f, MidiSignal_None, buffer, nullptr);
	ASSERT_EQ(message.messageCounter(), 0);
	ASSERT_EQ(message.timestamp(), 1234.0);

	uint8 stream1[16384];
	size_t size1;
	message.serialize(stream1, size1);

	auto loaded = JammerNetzMessage::deserialize(stream1, size1);
	ASSERT_EQ(loaded->getType(), JammerNetzMessage::AUDIODATA);

	// Serialize a second time
	uint8 stream2[16384];
	size_t size2;
	loaded->serialize(stream2, size2);
	ASSERT_EQ(size1, size2);

	auto loadedAudio1 = std::dynamic_pointer_cast<JammerNetzAudioData>(loaded);
	ASSERT_NE(loadedAudio1, nullptr);
	auto loaded2 = JammerNetzMessage::deserialize(stream2, size2);
	auto loadedAudio2 = std::dynamic_pointer_cast<JammerNetzAudioData>(loaded2);

	for (int channel = 0; channel < 2; channel++) {
		for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
			EXPECT_LE(fabs(loadedAudio2->audioBuffer()->getReadPointer(channel)[i] - loadedAudio1->audioBuffer()->getReadPointer(channel)[i]), 0.0001f);
		}
	}

}

TEST(TestProtocolCompatibility, CurrentPacketsAdvertiseSplitSessionProtocol)
{
	JammerNetzAudioData message(0, 1234.0, makeChannelSetup(), SAMPLE_RATE, 0.0f, MidiSignal_None, makeAudioBuffer(), nullptr);
	uint8 packet[16384];
	size_t packetSize = 0;
	message.serialize(packet, packetSize);

	flatbuffers::Verifier verifier(packet + sizeof(JammerNetzHeader), packetSize - sizeof(JammerNetzHeader));
	ASSERT_TRUE(VerifyJammerNetzPNPAudioDataBuffer(verifier));
	const auto *root = audioRoot(packet);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(root->protocolVersion(), JammerNetzProtocol::Current);
	EXPECT_TRUE(JammerNetzProtocol::supportsSplitSessionInfo(root->protocolVersion()));
}

TEST(TestProtocolCompatibility, CurrentPacketsKeepRc4LegacyVectorPresent)
{
	JammerNetzAudioData message(0, 1234.0, makeChannelSetup(), SAMPLE_RATE, 0.0f, MidiSignal_None, makeAudioBuffer(), nullptr);
	uint8 packet[16384];
	size_t packetSize = 0;
	message.serialize(packet, packetSize);

	const auto *root = audioRoot(packet);
	ASSERT_NE(root, nullptr);
	ASSERT_NE(root->audioBlocks(), nullptr);
	for (const auto *block : *root->audioBlocks()) {
		// rc4 dereferences allChannels() without checking it for null.
		ASSERT_NE(block->allChannels(), nullptr);
		EXPECT_EQ(block->allChannels()->size(), 0u);
	}
}

TEST(TestProtocolCompatibility, LegacyPacketsDefaultToVersionZeroAndExposeSessionSetup)
{
	auto legacySession = makeChannelSetup("Remote legacy participant");
	auto packet = makeLegacyPacket(legacySession, true);
	auto message = std::dynamic_pointer_cast<JammerNetzAudioData>(
		JammerNetzMessage::deserialize(packet.data(), packet.size()));

	ASSERT_NE(message, nullptr);
	EXPECT_EQ(message->protocolVersion(), JammerNetzProtocol::Legacy);
	EXPECT_FALSE(JammerNetzProtocol::supportsSplitSessionInfo(message->protocolVersion()));
	const auto decodedSession = message->legacySessionSetup();
	ASSERT_TRUE(decodedSession.has_value());
	ASSERT_EQ(decodedSession->channels.size(), 1u);
	EXPECT_EQ(decodedSession->channels.front().name, "Remote legacy participant");

	const auto prePadding = message->createPrePaddingPackage();
	EXPECT_EQ(prePadding->protocolVersion(), JammerNetzProtocol::Legacy);
	const auto prePaddingSession = prePadding->legacySessionSetup();
	ASSERT_TRUE(prePaddingSession.has_value());
	ASSERT_EQ(prePaddingSession->channels.size(), 1u);
	EXPECT_EQ(prePaddingSession->channels.front().name, "Remote legacy participant");

	bool hadFec = false;
	const auto fillIn = message->createFillInPackage(message->messageCounter() - 1, hadFec);
	EXPECT_TRUE(hadFec);
	EXPECT_EQ(fillIn->protocolVersion(), JammerNetzProtocol::Legacy);
	const auto fillInSession = fillIn->legacySessionSetup();
	ASSERT_TRUE(fillInSession.has_value());
	ASSERT_EQ(fillInSession->channels.size(), 1u);
	EXPECT_EQ(fillInSession->channels.front().name, "Remote legacy participant");
}

TEST(TestProtocolCompatibility, LegacyPacketsAllowMissingChannelNames)
{
	auto packet = makeLegacyPacket(makeChannelSetup("ignored"), false, true);
	auto message = std::dynamic_pointer_cast<JammerNetzAudioData>(
		JammerNetzMessage::deserialize(packet.data(), packet.size()));

	ASSERT_NE(message, nullptr);
	const auto decodedSession = message->legacySessionSetup();
	ASSERT_TRUE(decodedSession.has_value());
	ASSERT_EQ(decodedSession->channels.size(), 1u);
	EXPECT_TRUE(decodedSession->channels.front().name.empty());
}

TEST(TestProtocolCompatibility, ServerCanPopulateLegacySessionForRc4Client)
{
	JammerNetzAudioData message(0, 1234.0, makeChannelSetup(), SAMPLE_RATE, 0.0f, MidiSignal_None, makeAudioBuffer(), nullptr);
	message.setLegacySessionSetup(makeChannelSetup("Remote current participant"));
	uint8 packet[16384];
	size_t packetSize = 0;
	message.serialize(packet, packetSize);

	const auto *root = audioRoot(packet);
	ASSERT_NE(root, nullptr);
	ASSERT_NE(root->audioBlocks(), nullptr);
	ASSERT_GT(root->audioBlocks()->size(), 0u);
	const auto *legacyChannels = root->audioBlocks()->Get(0)->allChannels();
	ASSERT_NE(legacyChannels, nullptr);
	ASSERT_EQ(legacyChannels->size(), 1u);
	EXPECT_EQ(legacyChannels->Get(0)->name()->str(), "Remote current participant");
}

TEST(PacketStreamQueueTest, PopsOrderedPacketsInSequence)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(1)));
	ASSERT_TRUE(queue.push(makeQueuePacket(2)));
	EXPECT_EQ(queue.size(), 2u);

	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 1u);
	EXPECT_FALSE(isFillIn);
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 2u);
	EXPECT_FALSE(isFillIn);
	EXPECT_EQ(queue.size(), 0u);
	EXPECT_FALSE(queue.try_pop(packet, isFillIn));
}

TEST(PacketStreamQueueTest, ReordersOutOfOrderPackets)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(12)));
	ASSERT_TRUE(queue.push(makeQueuePacket(11)));

	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 11u);
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 12u);

	const auto quality = queue.qualityInfoPackage();
	EXPECT_EQ(quality.outOfOrderPacketCounter, 1u);
	EXPECT_EQ(quality.maxWrongOrderSpan, 1u);
}

TEST(PacketStreamQueueTest, RejectsDuplicatesAlreadyInTheQueue)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(7)));
	EXPECT_FALSE(queue.push(makeQueuePacket(7)));
	EXPECT_EQ(queue.size(), 1u);

	const auto quality = queue.qualityInfoPackage();
	EXPECT_EQ(quality.duplicatePacketCounter, 1u);
	EXPECT_EQ(quality.packagesPushed, 1u);
}

TEST(PacketStreamQueueTest, RejectsPacketsOlderThanTheLastPoppedPacket)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(10)));
	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));

	EXPECT_FALSE(queue.push(makeQueuePacket(9)));
	EXPECT_EQ(queue.size(), 0u);
	EXPECT_EQ(queue.qualityInfoPackage().tooLateOrDuplicate, 1u);
}

TEST(PacketStreamQueueTest, FillsOnePacketGapWithoutConsumingTheLaterPacket)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(20)));
	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));

	ASSERT_TRUE(queue.push(makeQueuePacket(22)));
	EXPECT_EQ(queue.size(), 1u);
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 21u);
	EXPECT_TRUE(isFillIn);
	EXPECT_EQ(queue.size(), 1u);

	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 22u);
	EXPECT_FALSE(isFillIn);
	EXPECT_EQ(queue.size(), 0u);
	const auto quality = queue.qualityInfoPackage();
	EXPECT_EQ(quality.droppedPacketCounter, 1u);
	EXPECT_EQ(quality.maxLengthOfGap, 1u);
	EXPECT_EQ(quality.packagesPushed, 2u);
	EXPECT_EQ(quality.packagesPopped, 2u);
}

TEST(PacketStreamQueueTest, ResetClearsPacketsStatisticsAndSequenceState)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(42)));
	ASSERT_TRUE(queue.push(makeQueuePacket(44)));
	queue.reset();

	EXPECT_EQ(queue.size(), 0u);
	const auto resetQuality = queue.qualityInfoPackage();
	EXPECT_EQ(resetQuality.packagesPushed, 0u);
	EXPECT_EQ(resetQuality.packagesPopped, 0u);
	EXPECT_EQ(resetQuality.outOfOrderPacketCounter, 0u);
	EXPECT_EQ(resetQuality.duplicatePacketCounter, 0u);

	ASSERT_TRUE(queue.push(makeQueuePacket(1)));
	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = true;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 1u);
	EXPECT_FALSE(isFillIn);
}
