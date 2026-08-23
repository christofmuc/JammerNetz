#include "JammerNetzPackage.h"
#include "JammerNetzClientInfoMessage.h"
#include "PacketStreamQueue.h"
#include "Encryption.h"

#include "BuffersConfig.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace {

void deterministicNonce(std::span<std::uint8_t, JammerNetzSecure::SecureDatagramSealer::NonceBytes> nonce)
{
	for (std::size_t index = 0; index < nonce.size(); ++index) {
		nonce[index] = static_cast<std::uint8_t>(index);
	}
}

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

TEST(PacketStreamQueueTest, RejectsDuplicateOfMostRecentlyPoppedPacket)
{
	PacketStreamQueue queue("test");
	ASSERT_TRUE(queue.push(makeQueuePacket(42)));

	std::shared_ptr<JammerNetzAudioData> popped;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(popped, isFillIn));
	ASSERT_NE(popped, nullptr);
	EXPECT_EQ(popped->messageCounter(), 42u);
	EXPECT_FALSE(isFillIn);

	EXPECT_FALSE(queue.push(makeQueuePacket(42)));
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

TEST(PacketStreamQueueTest, FastForwardRetainsNewestPacketsAndRebasesSequenceState)
{
	PacketStreamQueue queue("test");
	for (std::uint64_t counter = 100; counter <= 106; ++counter) {
		ASSERT_TRUE(queue.push(makeQueuePacket(counter)));
	}

	std::shared_ptr<JammerNetzAudioData> packet;
	bool isFillIn = false;
	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	ASSERT_EQ(packet->messageCounter(), 100u);

	const auto fastForward = queue.fastForwardToSize(3);
	EXPECT_EQ(fastForward.discardedPackets, 3u);
	ASSERT_TRUE(fastForward.oldestRetainedCounter.has_value());
	EXPECT_EQ(*fastForward.oldestRetainedCounter, 104u);
	EXPECT_EQ(queue.size(), 3u);

	ASSERT_TRUE(queue.try_pop(packet, isFillIn));
	EXPECT_EQ(packet->messageCounter(), 104u);
	EXPECT_FALSE(isFillIn);
	EXPECT_FALSE(queue.push(makeQueuePacket(103)));

	const auto quality = queue.qualityInfoPackage();
	EXPECT_EQ(quality.droppedPacketCounter, 3);
	EXPECT_EQ(quality.packagesPopped, 5u);
	EXPECT_EQ(quality.tooLateOrDuplicate, 1u);
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

TEST(ClientInfoTest, RoundTripsServerCapabilities)
{
	JammerNetzClientInfoMessage message;
	message.addClientInfo(IPAddress("127.0.0.1"), 7777, {});
	message.addCapability(JammerNetzCapability::MtuProbeV1);
	std::array<uint8, 4096> bytes {};
	size_t size = 0;
	message.serialize(bytes.data(), size);

	auto decoded = std::dynamic_pointer_cast<JammerNetzClientInfoMessage>(
		JammerNetzMessage::deserialize(bytes.data(), size));
	ASSERT_NE(decoded, nullptr);
	EXPECT_TRUE(decoded->supportsCapability(JammerNetzCapability::MtuProbeV1));
}

TEST(SessionInfoTest, RoundTripsServerCapabilities)
{
	JammerNetzSessionInfoMessage message;
	message.addCapability(JammerNetzCapability::MtuProbeV1);
	std::array<uint8, 4096> bytes {};
	size_t size = 0;
	message.serialize(bytes.data(), size);

	auto decoded = std::dynamic_pointer_cast<JammerNetzSessionInfoMessage>(
		JammerNetzMessage::deserialize(bytes.data(), size));
	ASSERT_NE(decoded, nullptr);
	EXPECT_TRUE(decoded->supportsCapability(JammerNetzCapability::MtuProbeV1));
}

TEST(SecureDatagramTest, InitializesLibsodium)
{
	EXPECT_TRUE(JammerNetzSecure::initializeCrypto());
}

TEST(SecureDatagramTest, RoundTripsOpaqueDirectionalDatagram)
{
	JammerNetzSecure::SessionId sessionId{};
	JammerNetzSecure::MasterKey masterKey{};
	JammerNetzSecure::SenderInstanceId senderId{};
	sessionId.fill(0x11);
	masterKey.fill(0x22);
	senderId.fill(0x33);
	auto key = std::make_shared<JammerNetzSecure::SessionKey>(sessionId, masterKey);
	JammerNetzSecure::SecureDatagramSealer sealer(key,
		JammerNetzSecure::Direction::ClientToServer, senderId, deterministicNonce);
	JammerNetzSecure::SecureDatagramOpener opener(key,
		JammerNetzSecure::Direction::ClientToServer);
	const std::array<std::uint8_t, 5> payload{{1, 2, 3, 4, 5}};
	std::array<std::uint8_t, 256> wire{};
	std::array<std::uint8_t, 32> opened{};

	const auto sealed = sealer.seal(payload, wire);
	ASSERT_TRUE(sealed);
	EXPECT_EQ(sealed.bytesWritten,
		payload.size() + JammerNetzSecure::SecureDatagramSealer::WireOverhead);
	constexpr char hexDigits[] = "0123456789abcdef";
	std::string actualVector;
	actualVector.reserve(sealed.bytesWritten * 2);
	for (std::size_t index = 0; index < sealed.bytesWritten; ++index) {
		actualVector.push_back(hexDigits[wire[index] >> 4]);
		actualVector.push_back(hexDigits[wire[index] & 0x0f]);
	}
	EXPECT_EQ(actualVector,
		"000102030405060708090a0b0c0d0e0f1011121314151617"
		"19a63907d9900d656247cc4b6c6e372ea8fa8d34302720ea2f"
		"d7196dca35465b16162e6dc02cefa267d850b59916fff1fe8e"
		"2341b4796df4c045b5964783782ff8");
	const auto ciphertextBegin = wire.begin()
		+ static_cast<std::ptrdiff_t>(JammerNetzSecure::SecureDatagramSealer::NonceBytes);
	const auto wireEnd = wire.begin() + static_cast<std::ptrdiff_t>(sealed.bytesWritten);
	EXPECT_EQ(std::search(ciphertextBegin, wireEnd, payload.begin(), payload.end()), wireEnd);
	const auto result = opener.open(
		std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), opened);
	ASSERT_TRUE(result);
	EXPECT_EQ(result.bytesWritten, payload.size());
	EXPECT_EQ(result.metadata.senderInstanceId, senderId);
	EXPECT_TRUE(result.metadata.advancedHighWatermark);
	EXPECT_TRUE(std::equal(payload.begin(), payload.end(), opened.begin()));
}

TEST(SecureDatagramTest, RejectsMutationWrongDirectionAndReplayWithoutReleasingPlaintext)
{
	JammerNetzSecure::SessionId sessionId{};
	JammerNetzSecure::MasterKey masterKey{};
	sessionId.fill(0x44);
	masterKey.fill(0x55);
	auto key = std::make_shared<JammerNetzSecure::SessionKey>(sessionId, masterKey);
	JammerNetzSecure::SecureDatagramSealer sealer(key, JammerNetzSecure::Direction::ClientToServer);
	const std::array<std::uint8_t, 4> payload{{9, 8, 7, 6}};
	std::array<std::uint8_t, 256> wire{};
	const auto sealed = sealer.seal(payload, wire);
	ASSERT_TRUE(sealed);

	JammerNetzSecure::SecureDatagramOpener wrongDirection(key,
		JammerNetzSecure::Direction::ServerToClient);
	std::array<std::uint8_t, 16> output{};
	output.fill(0xa5);
	EXPECT_EQ(wrongDirection.open(
		std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), output).error,
		JammerNetzSecure::SecureDatagramError::AuthenticationFailed);
	EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == 0xa5; }));
	auto wrongMasterKey = masterKey;
	wrongMasterKey[0] ^= 1;
	JammerNetzSecure::SecureDatagramOpener wrongKey(
		std::make_shared<JammerNetzSecure::SessionKey>(sessionId, wrongMasterKey),
		JammerNetzSecure::Direction::ClientToServer);
	EXPECT_EQ(wrongKey.open(
		std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), output).error,
		JammerNetzSecure::SecureDatagramError::AuthenticationFailed);
	JammerNetzSecure::SecureDatagramOpener mutationOpener(key,
		JammerNetzSecure::Direction::ClientToServer);
	for (std::size_t index = 0; index < sealed.bytesWritten; ++index) {
		auto mutated = wire;
		mutated[index] ^= 1;
		output.fill(0xa5);
		EXPECT_EQ(mutationOpener.open(
			std::span<const std::uint8_t>(mutated.data(), sealed.bytesWritten), output).error,
			JammerNetzSecure::SecureDatagramError::AuthenticationFailed) << "byte " << index;
		EXPECT_TRUE(std::all_of(output.begin(), output.end(),
			[](auto byte) { return byte == 0xa5; })) << "byte " << index;
	}

	JammerNetzSecure::SecureDatagramOpener opener(key,
		JammerNetzSecure::Direction::ClientToServer);
	ASSERT_TRUE(opener.open(std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), output));
	EXPECT_EQ(opener.open(std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), output).error,
		JammerNetzSecure::SecureDatagramError::Replay);
	wire[sealed.bytesWritten - 1] ^= 1;
	output.fill(0xa5);
	EXPECT_EQ(opener.open(std::span<const std::uint8_t>(wire.data(), sealed.bytesWritten), output).error,
		JammerNetzSecure::SecureDatagramError::AuthenticationFailed);
	EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == 0xa5; }));
}

TEST(ReplayWindowTest, AcceptsReorderingAndRejectsDuplicatesAndOldCounters)
{
	JammerNetzSecure::ReplayWindow window;
	EXPECT_TRUE(window.accept(100).accepted);
	EXPECT_TRUE(window.accept(102).advancedHighWatermark);
	const auto reordered = window.accept(101);
	EXPECT_TRUE(reordered.accepted);
	EXPECT_FALSE(reordered.advancedHighWatermark);
	EXPECT_FALSE(window.accept(101).accepted);
	EXPECT_TRUE(window.accept(230).accepted);
	EXPECT_FALSE(window.accept(100).accepted);
}

TEST(SessionKeyTest, GeneratesLoadsAndStrictlyRejectsOverwriteAndTrailingData)
{
	const auto file = File::getSpecialLocation(File::tempDirectory)
		.getNonexistentChildFile("jammernetz-session", ".jnzkey", false);
	const std::filesystem::path path(file.getFullPathName().toStdString());
	std::string error;
	ASSERT_TRUE(JammerNetzSecure::SessionKey::generate(path, false, error)) << error;
	EXPECT_FALSE(JammerNetzSecure::SessionKey::generate(path, false, error));
	auto loaded = JammerNetzSecure::SessionKey::load(path, error);
	ASSERT_NE(loaded, nullptr) << error;
	EXPECT_EQ(loaded->fingerprint().size(), 16u);
	{
		std::ofstream append(path, std::ios::binary | std::ios::app);
		append.put('\0');
	}
	EXPECT_EQ(JammerNetzSecure::SessionKey::load(path, error), nullptr);
	EXPECT_TRUE(file.deleteFile());
}

TEST(SecureDatagramTest, CounterExhaustionIsPermanent)
{
	JammerNetzSecure::SessionId sessionId{};
	JammerNetzSecure::MasterKey masterKey{};
	JammerNetzSecure::SenderInstanceId senderId{};
	sessionId.fill(1);
	masterKey.fill(2);
	senderId.fill(3);
	auto key = std::make_shared<JammerNetzSecure::SessionKey>(sessionId, masterKey);
	JammerNetzSecure::SecureDatagramSealer sealer(key,
		JammerNetzSecure::Direction::ClientToServer, senderId, deterministicNonce,
		std::numeric_limits<std::uint64_t>::max());
	std::array<std::uint8_t, 128> wire{};
	const std::array<std::uint8_t, 1> payload{{42}};
	EXPECT_EQ(sealer.seal(payload, wire).error,
		JammerNetzSecure::SecureDatagramError::CounterExhausted);
	EXPECT_EQ(sealer.seal(payload, wire).error,
		JammerNetzSecure::SecureDatagramError::CounterExhausted);
}
