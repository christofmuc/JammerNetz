#include "JammerNetzPackage.h"

#include "BuffersConfig.h"

#include "gtest/gtest.h"

TEST(TestSerialization, TestAudioData) {
	auto buffer = std::make_shared<AudioBuffer<float>>(2, SAMPLE_BUFFER_SIZE);
	// Create some AudioBuffer
	for (int channel = 0; channel < 2; channel++) {
		auto samples = buffer->getWritePointer(channel);
		for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
			samples[i] = (float)i / 3.0f;
		}
	}

	// Create a setup for this
	JammerNetzChannelSetup setup(false);
	setup.channels.push_back(JammerNetzSingleChannelSetup(JammerNetzChannelTarget::Left));

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
