/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "flatbuffers/flatbuffers.h"
#include "JammerNetzAudioData_generated.h"


const size_t MAXFRAMESIZE = 65536;

/*
 For lazi-ness and stateless-ness Jammernetz works with only a single message type for now. This is the format:

  | AUDIODATA type message - this is sent by the client to the server, and also the server sends its mixing result with this type of message
  | JammerNetzAudioHeader
  |                                  | Audio - 2 Blocks, one active data and one FEC block containing the previous active block
  | JammerNetzHeader                 | JammerNetzAudioBlock                                                                     | AudioData for Block                       |
  | magic0 magic1 magic2 messageType | timestamp messageCounter channelSetup            numChannels numberOfSamples sampleRate  | numChannels * numberOfSamples audio bytes | 
  | uint8  uint8  uint8  uint8       | double    uint64         JammerNetzChannelSetup  uint8       uint16          uint16      | uint16                                    |

*/

struct JammerNetzHeader {
	uint8 magic0;
	uint8 magic1;
	uint8 magic2;
	uint8 messageType;
};

enum JammerNetzChannelTarget {
	Unused = 0,
	Left,
	Right, 
	Mono,
	SendOnly,
};

struct JammerNetzSingleChannelSetup {
	JammerNetzSingleChannelSetup();
	JammerNetzSingleChannelSetup(uint8 target);
	uint8 target;
	float volume;
	float balanceLeftRight;

	bool operator ==(const JammerNetzSingleChannelSetup &other) const;
};

struct JammerNetzChannelSetup {
	JammerNetzChannelSetup();
	JammerNetzChannelSetup(std::vector<JammerNetzSingleChannelSetup> const &channelInfo);
	std::vector<JammerNetzSingleChannelSetup> channels;

	bool operator ==(const JammerNetzChannelSetup &other) const;
};

struct JammerNetzAudioBlock {
	double timestamp; // Using JUCE's high resolution timer
	juce::uint64 messageCounter;
	JammerNetzChannelSetup channelSetup;
	uint8 numchannels;
	uint16 numberOfSamples;
	uint16 sampleRate;
};

struct JammerNetzAudioHeader {
	JammerNetzHeader header;
	JammerNetzAudioBlock audioBlock[1];
};

struct AudioBlock {
	AudioBlock() = default;
	AudioBlock(AudioBlock const &other) = default;
	AudioBlock(double timestamp, uint64 messageCounter, uint64 serverTime, uint16 sampleRate, JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer);
	double timestamp; // Using JUCE's high resolution timer
	juce::uint64 messageCounter;
	juce::uint64 serverTime;
	juce::uint64 serverTimeSampleBased;
	uint16 sampleRate;
	JammerNetzChannelSetup channelSetup;
	std::shared_ptr<AudioBuffer<float>> audioBuffer;
};

class JammerNetzMessage {
public:
	enum MessageType {
		AUDIODATA = 1,
		CLIENTINFO = 8,
	};

	virtual MessageType getType() const = 0;

	virtual void serialize(uint8 *output, size_t &byteswritten) const = 0;
	static std::shared_ptr<JammerNetzMessage> deserialize(uint8 *data, size_t bytes);

protected:
	int writeHeader(uint8 *output, uint8 messageType) const;
};

class JammerNetzAudioData : public JammerNetzMessage {
public:
	JammerNetzAudioData(uint8 *data, size_t bytes);
	JammerNetzAudioData(uint64 messageCounter, double timestamp, JammerNetzChannelSetup const &channelSetup, int sampleRate, std::shared_ptr<AudioBuffer<float>> audioBuffer, std::shared_ptr<AudioBlock> fecBlock);
	JammerNetzAudioData(AudioBlock const &audioBlock, std::shared_ptr<AudioBlock> fecBlock);

	std::shared_ptr<JammerNetzAudioData> createFillInPackage(uint64 messageNumber) const;
	std::shared_ptr<JammerNetzAudioData> createPrePaddingPackage() const;

	virtual MessageType getType() const override;

	virtual void serialize(uint8 *output, size_t &byteswritten) const override;

	// Read access, those use the "active block"
	std::shared_ptr<AudioBuffer<float>> audioBuffer() const;
	uint64 messageCounter() const;
	double timestamp() const;
	uint64 serverTime() const;
	JammerNetzChannelSetup channelSetup() const;

private:
	flatbuffers::Offset<JammerNetzPNPAudioBlock> serializeAudioBlock(flatbuffers::FlatBufferBuilder &fbb, std::shared_ptr<AudioBlock> src, uint16 sampleRate, uint16 reductionFactor) const;
	flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>>> appendAudioBuffer(flatbuffers::FlatBufferBuilder &fbb, AudioBuffer<float> &buffer, uint16 reductionFactor) const;
	std::shared_ptr<AudioBlock> readAudioHeaderAndBytes(JammerNetzPNPAudioBlock const *block);
	void readAudioBytes(flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples >> const *samples, std::shared_ptr<AudioBuffer<float>> destBuffer, int upsampleRate);

	std::shared_ptr<AudioBlock> audioBlock_;
	std::shared_ptr<AudioBlock> fecBlock_;
	std::shared_ptr<AudioBlock> activeBlock_;
};

class JammerNetzAudioOrder {
public:
	bool operator() (std::shared_ptr<JammerNetzAudioData> const &data1, std::shared_ptr<JammerNetzAudioData> const &data2) {
		return data1->messageCounter() > data2->messageCounter();
	}
};
