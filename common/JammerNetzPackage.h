/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "flatbuffers/flatbuffers.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "JammerNetzAudioData_generated.h"
#include "JammerNetzSessionInfo_generated.h"
#include "JammerNetzControlMessage_generated.h"

#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <memory>

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
	Mute = 0,
	Left,
	Right,
	Mono,
	SendMono,
	SendLeft,
	SendRight
};

struct JammerNetzSingleChannelSetup {
	JammerNetzSingleChannelSetup();
	explicit JammerNetzSingleChannelSetup(uint8 target);
	uint8 target;
	float volume;
	float mag;
	float rms;
	float pitch;
	std::string name;

	[[nodiscard]] bool isEqualEnough(const JammerNetzSingleChannelSetup &other) const;
};

struct JammerNetzChannelSetup {
	explicit JammerNetzChannelSetup(bool localMonitoring);
	JammerNetzChannelSetup(bool localMonitoring, std::vector<JammerNetzSingleChannelSetup> const &channelInfo);
	bool isLocalMonitoringDontSendEcho;
	std::vector<JammerNetzSingleChannelSetup> channels;

	[[nodiscard]] bool isEqualEnough(const JammerNetzChannelSetup &other) const;
};

struct JammerNetzAudioBlock {
	double timestamp{}; // Using JUCE's high resolution timer
	juce::uint64 messageCounter{};
	JammerNetzChannelSetup channelSetup;
	uint8 numchannels{};
	uint16 numberOfSamples{};
	uint16 sampleRate{};
};

struct JammerNetzAudioHeader {
	JammerNetzHeader header;
	JammerNetzAudioBlock audioBlock[1];
};

struct AudioBlock {
	AudioBlock() : channelSetup(false) {
	}

	//AudioBlock(AudioBlock const &other) = default;
	AudioBlock(double timestamp, uint64 messageCounter, uint64 serverTime, float bpm, MidiSignal midiSignal, uint16 sampleRate, JammerNetzChannelSetup const &channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer);
	double timestamp; // Using JUCE's high resolution timer
	juce::uint64 messageCounter;
	juce::uint64 serverTime;
	juce::uint64 serverTimeSampleBased;
	float bpm;
	MidiSignal midiSignal;
	uint16 sampleRate;
	JammerNetzChannelSetup channelSetup;
	std::shared_ptr<AudioBuffer<float>> audioBuffer;
};

struct JammerNetzMessageParseException : public std::exception {
	[[nodiscard]] const char *what() const noexcept
	{
		return "JammerNetz Message Parse Error";
	}
};

class JammerNetzMessage {
public:
	enum MessageType {
		AUDIODATA = 1,
		CLIENTINFO = 8,
        SESSIONSETUP = 16,
        GENERIC_JSON = 32,
	};

    JammerNetzMessage() = default;
    JammerNetzMessage(JammerNetzMessage const&) = default;
    virtual ~JammerNetzMessage() = default;

    [[nodiscard]] virtual MessageType getType() const = 0;

	virtual void serialize(uint8 *output, size_t &byteswritten) const = 0;
	static std::shared_ptr<JammerNetzMessage> deserialize(uint8 *data, size_t bytes);

protected:
	size_t writeHeader(uint8 *output, uint8 messageType) const;
};

template<JammerNetzMessage::MessageType ID>
class JammerNetzFlatbufferMessage : public JammerNetzMessage {
public:
    JammerNetzFlatbufferMessage() = default;

    // Generic deserialization constructor
    JammerNetzFlatbufferMessage(size_t size) {
        if (size < sizeof(JammerNetzAudioHeader)) {
            throw JammerNetzMessageParseException();
        }
    }

    void serialize(uint8 *output, size_t &byteswritten) const override
    {
        byteswritten = writeHeader(output, getType());
        flatbuffers::FlatBufferBuilder fbb;
        serializeToFlatbuffer(fbb);
        memcpy(output + byteswritten, fbb.GetBufferPointer(), fbb.GetSize());
        byteswritten += fbb.GetSize();
    }

    virtual void serializeToFlatbuffer(flatbuffers::FlatBufferBuilder &fbb) const = 0;

    [[nodiscard]] MessageType getType() const override
    {
        return ID;
    }
};

class JammerNetzControlMessage : public JammerNetzFlatbufferMessage<JammerNetzMessage::MessageType::GENERIC_JSON>
{
public:
    JammerNetzControlMessage(nlohmann::json &json) : json_(json)
    {
    }

    JammerNetzControlMessage(uint8 *data, size_t size) : JammerNetzFlatbufferMessage<JammerNetzMessage::MessageType::GENERIC_JSON>(size) {
        uint8* dataStart = data + sizeof(JammerNetzHeader);
        flatbuffers::Verifier verifier(dataStart, size - sizeof(JammerNetzHeader));
        if (VerifyJammerNetzControlInfoBuffer(verifier)) {
            auto buffer = GetJammerNetzControlInfo(dataStart);
            try {
                json_ = nlohmann::json::parse(buffer->control_message_json()->str());
            }
            catch (nlohmann::json::parse_error &e) {
                throw JammerNetzMessageParseException();
            }
        }
    }

    void serializeToFlatbuffer(flatbuffers::FlatBufferBuilder &fbb) const override {
        auto fb_json = fbb.CreateString(json_.dump());
        fbb.Finish(CreateJammerNetzControlInfo(fbb, fb_json));
    }

    nlohmann::json json_;
};

class JammerNetzSessionInfoMessage : public JammerNetzFlatbufferMessage<JammerNetzMessage::MessageType::SESSIONSETUP>
{
public:
    JammerNetzSessionInfoMessage() : channels_(false)
    {
    }

    JammerNetzSessionInfoMessage(uint8 *data, size_t size) : JammerNetzFlatbufferMessage<JammerNetzMessage::MessageType::SESSIONSETUP>(size), channels_(false) {
        uint8* dataStart = data + sizeof(JammerNetzHeader);
        flatbuffers::Verifier verifier(dataStart, size - sizeof(JammerNetzHeader));
        if (VerifyJammerNetzSessionInfoBuffer(verifier)) {
            auto package = GetJammerNetzSessionInfo(dataStart);
            channels_.channels.clear();
            for (auto channel = package->allChannels()->cbegin(); channel != package->allChannels()->cend(); channel++) {
                JammerNetzSingleChannelSetup setup(channel->target());
                setup.volume = channel->volume();
                setup.mag = channel->mag();
                setup.rms = channel->rms();
                setup.pitch = channel->pitch();
                setup.name = channel->name()->str();
                channels_.channels.push_back(setup);
            }
        }
    }

    void serializeToFlatbuffer(flatbuffers::FlatBufferBuilder &fbb) const override
    {
        std::vector<flatbuffers::Offset<JammerNetzPNPChannelSetup>> allChannels;
        for (const auto& channel : channels_.channels) {
            auto fb_name = fbb.CreateString(channel.name);
            allChannels.push_back(CreateJammerNetzPNPChannelSetup(fbb, channel.target, channel.volume, channel.mag, channel.rms, channel.pitch, fb_name));
        }
        auto channelSetupVector = fbb.CreateVector(allChannels);
        fbb.Finish(CreateJammerNetzSessionInfo(fbb, channelSetupVector));
    }

    JammerNetzChannelSetup channels_;
};

class JammerNetzAudioData : public JammerNetzMessage {
public:
	JammerNetzAudioData(uint8 *data, size_t bytes);
	JammerNetzAudioData(uint64 messageCounter, double timestamp, JammerNetzChannelSetup const &channelSetup, int sampleRate, std::optional<float> bpm, MidiSignal midiSignal, std::shared_ptr<AudioBuffer<float>> audioBuffer, std::shared_ptr<AudioBlock> fecBlock);
	JammerNetzAudioData(AudioBlock const &audioBlock, std::shared_ptr<AudioBlock> fecBlock);

	std::shared_ptr<JammerNetzAudioData> createFillInPackage(uint64 messageNumber, bool &outHadFEC) const;
	std::shared_ptr<JammerNetzAudioData> createPrePaddingPackage() const;

	virtual MessageType getType() const override;

	virtual void serialize(uint8 *output, size_t &byteswritten) const override;

	// Read access, those use the "active block"
	std::shared_ptr<AudioBuffer<float>> audioBuffer() const;
	uint64 messageCounter() const;
	double timestamp() const;
	uint64 serverTime() const;
	float bpm() const;
	MidiSignal midiSignal() const;
	JammerNetzChannelSetup channelSetup() const;

private:
	flatbuffers::Offset<JammerNetzPNPAudioBlock> serializeAudioBlock(flatbuffers::FlatBufferBuilder &fbb, std::shared_ptr<AudioBlock> src, uint16 sampleRate, uint16 reductionFactor) const;
	flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>>> appendAudioBuffer(flatbuffers::FlatBufferBuilder &fbb, AudioBuffer<float> &buffer, uint16 reductionFactor) const;
	std::shared_ptr<AudioBlock> readAudioHeaderAndBytes(JammerNetzPNPAudioBlock const *block);
	void readAudioBytes(flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples >> const *samples, std::shared_ptr<AudioBuffer<float>> destBuffer, size_t upsampleRate);

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
