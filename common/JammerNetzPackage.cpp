/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JammerNetzPackage.h"

#include "BuffersConfig.h"

#include "JammerNetzClientInfoMessage.h"

JammerNetzSingleChannelSetup::JammerNetzSingleChannelSetup() :
	target(JammerNetzChannelTarget::Mono), volume(1.0f), mag(0.0f), rms(0.0f), pitch(0.0f), name("")
{
}

JammerNetzSingleChannelSetup::JammerNetzSingleChannelSetup(uint8 target_) :
	target(target_), volume(1.0f), mag(0.0f), rms(0.0f), pitch(0.0f), name("")
{
}


bool JammerNetzSingleChannelSetup::isEqualEnough(const JammerNetzSingleChannelSetup &other) const
{
	return target == other.target && fabs(volume-other.volume) < 1e-6f && name == other.name;
}

/*bool JammerNetzSingleChannelSetup::operator==(const JammerNetzSingleChannelSetup &other) const
{
	return target == other.target && volume == other.volume;
}*/

JammerNetzChannelSetup::JammerNetzChannelSetup(bool localMonitoring) : isLocalMonitoringDontSendEcho(localMonitoring)
{
}

JammerNetzChannelSetup::JammerNetzChannelSetup(bool localMonitoring, std::vector<JammerNetzSingleChannelSetup> const &channelInfo) : isLocalMonitoringDontSendEcho(localMonitoring)
{
	channels = channelInfo;
}

bool JammerNetzChannelSetup::isEqualEnough(const JammerNetzChannelSetup &other) const
{
	if (isLocalMonitoringDontSendEcho != other.isLocalMonitoringDontSendEcho) return false;
	if (channels.size() != other.channels.size()) return false;
	for (size_t i = 0; i < channels.size(); i++) {
		if (!(channels[i].isEqualEnough(other.channels[i]))) return false;
	}
	return true;
}

AudioBlock::AudioBlock(double timestamp_, uint64 messageCounter_, uint64 serverTime_, float bpm_, MidiSignal midiSignal_, uint16 sampleRate_, JammerNetzChannelSetup const &channelSetup_,
                       std::shared_ptr<AudioBuffer<float>> audioBuffer_) :
	timestamp(timestamp_), messageCounter(messageCounter_), serverTime(serverTime_), bpm(bpm_), midiSignal(midiSignal_), sampleRate(sampleRate_), channelSetup(channelSetup_), audioBuffer(audioBuffer_)
{
}

std::shared_ptr<JammerNetzMessage> JammerNetzMessage::deserialize(uint8 *data, size_t bytes)
{
	if (bytes >= sizeof(JammerNetzHeader)) {
		auto header = reinterpret_cast<JammerNetzHeader*>(data);

		try {
			// Check the magic
			if (header->magic0 == '1' && header->magic1 == '2' && header->magic2 == '3') {
				switch (header->messageType) {
				case AUDIODATA:
					return std::make_shared<JammerNetzAudioData>(data, bytes);
				case CLIENTINFO:
					return std::make_shared<JammerNetzClientInfoMessage>(data, bytes);
                case SESSIONSETUP:
                    return std::make_shared<JammerNetzSessionInfoMessage>(data, bytes);
                case GENERIC_JSON:
                    return std::make_shared<JammerNetzControlMessage>(data, bytes);
				default:
					std::cerr << "Unknown message type received, ignoring it" << std::endl;
				}
			}
		}
		catch (JammerNetzMessageParseException &) {
			// Invalid, probably tampered with or old 1.0 package
			return nullptr;
		}
	}
	return nullptr;;
}

size_t JammerNetzMessage::writeHeader(uint8 *output, uint8 messageType) const
{
	JammerNetzHeader *header = reinterpret_cast<JammerNetzHeader*>(output);
	header->magic0 = '1';
	header->magic1 = '2';
	header->magic2 = '3';
	header->messageType = messageType;
	return sizeof(JammerNetzHeader);
}

// Deserializing constructor
JammerNetzAudioData::JammerNetzAudioData(uint8 *data, size_t bytes) {
	if (bytes < sizeof(JammerNetzAudioHeader)) {
		throw JammerNetzMessageParseException();
		return;
	}

	flatbuffers::Verifier verifier(data + sizeof(JammerNetzHeader), bytes - sizeof(JammerNetzHeader));
	if (VerifyJammerNetzPNPAudioDataBuffer(verifier)) {
		auto root = GetJammerNetzPNPAudioData(data + sizeof(JammerNetzHeader));
		int blockNo = 0;
		for (auto block = root->audioBlocks()->cbegin(); block != root->audioBlocks()->cend(); block++) {
			if (blockNo == 0) {
				audioBlock_ = readAudioHeaderAndBytes(*block);
				activeBlock_ = audioBlock_;
			}
			else if (blockNo == 1) {
				fecBlock_ = readAudioHeaderAndBytes(*block);
			}
			else {
				jassertfalse;
				throw JammerNetzMessageParseException();
			}
			blockNo++;
		}
	}
	else {
		throw JammerNetzMessageParseException();
	}
}

JammerNetzAudioData::JammerNetzAudioData(uint64 messageCounter, double timestamp, JammerNetzChannelSetup const &channelSetup, int sampleRate, std::optional<float> bpm,
	MidiSignal midiSignal,
    std::shared_ptr<AudioBuffer<float>> audioBuffer, std::shared_ptr<AudioBlock> fecBlock) :
	fecBlock_(fecBlock)
{
	audioBlock_ = std::make_shared<AudioBlock>();
	audioBlock_->messageCounter = messageCounter;
	audioBlock_->timestamp = timestamp;
	audioBlock_->sampleRate = (uint16) sampleRate; // What about 96kHz?
	if (bpm.has_value()) {
		audioBlock_->bpm = *bpm;
	}
	audioBlock_->midiSignal = midiSignal;
	audioBlock_->channelSetup = channelSetup;
	audioBlock_->audioBuffer = audioBuffer;
	activeBlock_ = audioBlock_;
}

JammerNetzAudioData::JammerNetzAudioData(AudioBlock const &audioBlock, std::shared_ptr<AudioBlock> fecBlock) : fecBlock_(fecBlock)
{
	audioBlock_ = std::make_shared<AudioBlock>(audioBlock);
	activeBlock_ = audioBlock_;
}

std::shared_ptr<JammerNetzAudioData> JammerNetzAudioData::createFillInPackage(uint64 messageNumber, bool &outHadFEC) const
{
	if (fecBlock_) {
		outHadFEC = true;
		return std::make_shared<JammerNetzAudioData>(messageNumber, fecBlock_->timestamp, fecBlock_->channelSetup, SAMPLE_RATE, fecBlock_->bpm, fecBlock_->midiSignal, fecBlock_->audioBuffer, nullptr);
	}
	// No FEC data available, fall back to "repeat last package"
	//TODO - fake timestamp?
	outHadFEC = false;
	return std::make_shared<JammerNetzAudioData>(messageNumber, audioBlock_->timestamp, audioBlock_->channelSetup, SAMPLE_RATE, audioBlock_->bpm, audioBlock_->midiSignal, audioBlock_->audioBuffer, nullptr);
}

std::shared_ptr<JammerNetzAudioData> JammerNetzAudioData::createPrePaddingPackage() const
{
	// When a client connects, we want a certain number of packages in the queue, else it will run empty again and the client will be disconnected immediately.
	auto silence = std::make_shared<AudioBuffer<float>>();
	*silence = *audioBlock_->audioBuffer; // Deep copy
	silence->clear();
	return std::make_shared<JammerNetzAudioData>(audioBlock_->messageCounter - 1, audioBlock_->timestamp, audioBlock_->channelSetup, SAMPLE_RATE, std::optional<float>(),
		MidiSignal_None,
	    silence, nullptr);
}

JammerNetzMessage::MessageType JammerNetzAudioData::getType() const
{
	return AUDIODATA;
}

void JammerNetzAudioData::serialize(uint8 *output, size_t &byteswritten) const {
	jassert(audioBlock_);
	byteswritten = writeHeader(output, AUDIODATA);

	flatbuffers::FlatBufferBuilder fbb;
	std::vector<flatbuffers::Offset<JammerNetzPNPAudioBlock>> audioBlocks;

	audioBlocks.push_back(serializeAudioBlock(fbb, audioBlock_, 48000, 1));
	if (fecBlock_) {
		audioBlocks.push_back(serializeAudioBlock(fbb, fecBlock_, 48000, FEC_SAMPLERATE_REDUCTION));
	}

	auto blockVec = fbb.CreateVector(audioBlocks);
	JammerNetzPNPAudioDataBuilder audioData(fbb);
	audioData.add_audioBlocks(blockVec);

	fbb.Finish(audioData.Finish());
	memcpy(output + byteswritten, fbb.GetBufferPointer(), fbb.GetSize());
	byteswritten += fbb.GetSize();
}

flatbuffers::Offset<JammerNetzPNPAudioBlock> JammerNetzAudioData::serializeAudioBlock(flatbuffers::FlatBufferBuilder &fbb, std::shared_ptr<AudioBlock> src, uint16 sampleRate, uint16 reductionFactor) const
{
	std::vector<flatbuffers::Offset<JammerNetzPNPChannelSetup>> channelSetup;
	for (const auto& channel : src->channelSetup.channels) {
		auto fb_name = fbb.CreateString(channel.name);
		channelSetup.push_back(CreateJammerNetzPNPChannelSetup(fbb, channel.target, channel.volume, channel.mag, channel.rms, channel.pitch, fb_name));
	}
	auto channelSetupVector = fbb.CreateVector(channelSetup);
	auto audioSamples = appendAudioBuffer(fbb, *src->audioBuffer, reductionFactor);

	JammerNetzPNPAudioBlockBuilder audioBlock(fbb);
	audioBlock.add_timestamp(src->timestamp);
	audioBlock.add_messageCounter(src->messageCounter);
	audioBlock.add_serverTime(src->serverTime);
	audioBlock.add_bpm(src->bpm);
	audioBlock.add_midiSignal(src->midiSignal);
	audioBlock.add_numberOfSamples((uint16)src->audioBuffer->getNumSamples() / reductionFactor);
	audioBlock.add_numChannels((uint8)src->audioBuffer->getNumChannels());
	audioBlock.add_sampleRate(sampleRate / reductionFactor);
	audioBlock.add_channelSetup(channelSetupVector);
	audioBlock.add_channels(audioSamples);
	audioBlock.add_wantEcho(!src->channelSetup.isLocalMonitoringDontSendEcho);

	return audioBlock.Finish();
}

flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>>> JammerNetzAudioData::appendAudioBuffer(flatbuffers::FlatBufferBuilder &fbb, AudioBuffer<float> &buffer, uint16 reductionFactor) const {
	std::vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>> channels;
	for (int inputChannel = 0; inputChannel < buffer.getNumChannels(); inputChannel++) {
		if (reductionFactor == 1) {
			AudioData::Pointer<AudioData::Float32, AudioData::LittleEndian, AudioData::NonInterleaved, AudioData::Const> inputData(buffer.getReadPointer(inputChannel));
			uint16 *outputBuffer;
			auto singleChannelVector = fbb.CreateUninitializedVector((size_t) buffer.getNumSamples(), &outputBuffer);
			AudioData::Pointer<AudioData::Int16, AudioData::LittleEndian, AudioData::NonInterleaved, AudioData::NonConst> dataToSend(outputBuffer);
			dataToSend.convertSamples(inputData, buffer.getNumSamples());
			channels.push_back(CreateJammerNetzPNPAudioSamples(fbb, singleChannelVector));
		}
		else {
			float tempBuffer[MAXFRAMESIZE];
			const float* read = buffer.getReadPointer(inputChannel);

			int outputSamples = buffer.getNumSamples() / 2;
			for (int i = 0; i < buffer.getNumSamples(); i += reductionFactor) {
				tempBuffer[i / reductionFactor] = read[i];
			}

			AudioData::Pointer<AudioData::Float32, AudioData::LittleEndian, AudioData::NonInterleaved, AudioData::Const> inputData(tempBuffer);
			uint16 *outputBuffer;
			auto singleChannelVector = fbb.CreateUninitializedVector((size_t)buffer.getNumSamples()/reductionFactor, &outputBuffer);
			AudioData::Pointer<AudioData::Int16, AudioData::LittleEndian, AudioData::NonInterleaved, AudioData::NonConst> dataToSend(outputBuffer);
			dataToSend.convertSamples(inputData, outputSamples);
			channels.push_back(CreateJammerNetzPNPAudioSamples(fbb, singleChannelVector));
		}
	}
	return fbb.CreateVector(channels);
}

std::shared_ptr<juce::AudioBuffer<float>> JammerNetzAudioData::audioBuffer() const
{
	return activeBlock_->audioBuffer;
}

juce::uint64 JammerNetzAudioData::messageCounter() const
{
	return activeBlock_->messageCounter;
}

double JammerNetzAudioData::timestamp() const
{
	return activeBlock_->timestamp;
}

juce::uint64 JammerNetzAudioData::serverTime() const
{
	return activeBlock_->serverTime;
}

float JammerNetzAudioData::bpm() const
{
	return activeBlock_->bpm;
}

MidiSignal JammerNetzAudioData::midiSignal() const
{
	return activeBlock_->midiSignal;
}

JammerNetzChannelSetup JammerNetzAudioData::channelSetup() const
{
	return activeBlock_->channelSetup;
}

std::shared_ptr<AudioBlock> JammerNetzAudioData::readAudioHeaderAndBytes(JammerNetzPNPAudioBlock const *block) {
	auto result = std::make_shared<AudioBlock>();

	result->messageCounter = block->messageCounter();
	result->serverTime = block->serverTime();
	result->bpm = block->bpm();
	result->midiSignal = block->midiSignal();
	result->timestamp = block->timestamp();
	for (auto channel = block->channelSetup()->cbegin(); channel != block->channelSetup()->cend(); channel++) {
		JammerNetzSingleChannelSetup setup(channel->target());
		setup.volume = channel->volume();
		setup.mag = channel->mag();
		setup.rms = channel->rms();
		setup.pitch = channel->pitch();
		setup.name = channel->name()->str();
		result->channelSetup.channels.push_back(setup);
	}

	result->sampleRate = 48000;
	size_t upsampleRate = block->sampleRate() != 0 ? 48000 / block->sampleRate() : 48000;
	jassert(block->numberOfSamples() * upsampleRate == SAMPLE_BUFFER_SIZE);
	result->audioBuffer = std::make_shared<AudioBuffer<float>>((int) block->numChannels(), (int) (block->numberOfSamples() * upsampleRate));
	readAudioBytes(block->channels(), result->audioBuffer, upsampleRate);
	return result;
}

void JammerNetzAudioData::readAudioBytes(flatbuffers::Vector<flatbuffers::Offset<JammerNetzPNPAudioSamples>> const *samples, std::shared_ptr<AudioBuffer<float>> destBuffer, size_t upsampleRate) {
	int c = 0;
	for (auto channel = samples->cbegin(); channel != samples->cend(); channel++) {
		//TODO we might not have enough bytes in the package for this operation
		if (upsampleRate == 1) {

			AudioData::Pointer <AudioData::Int16,
				AudioData::LittleEndian,
				AudioData::NonInterleaved,
				AudioData::Const> src_pointer(channel->audioSamples()->data());
			AudioData::Pointer<AudioData::Float32,
				AudioData::LittleEndian,
				AudioData::NonInterleaved,
				AudioData::NonConst> dst_pointer(destBuffer->getWritePointer(c));
			dst_pointer.convertSamples(src_pointer, (int) channel->audioSamples()->size());
		}
		else {
			float tempBuffer[MAXFRAMESIZE];
			AudioData::Pointer <AudioData::Int16,
				AudioData::LittleEndian,
				AudioData::NonInterleaved,
				AudioData::Const> src_pointer(channel->audioSamples()->data());
			AudioData::Pointer<AudioData::Float32,
				AudioData::LittleEndian,
				AudioData::NonInterleaved,
				AudioData::NonConst> dst_pointer(tempBuffer);
			dst_pointer.convertSamples(src_pointer, (int) channel->audioSamples()->size());

			auto write = destBuffer->getWritePointer(c);
			for (size_t i = 0; i < channel->audioSamples()->size(); i++) {
				for (size_t j = 0; j < upsampleRate; j++) {
					write[i * upsampleRate + j] = tempBuffer[i];
				}
			}
		}
		c++;
	}
}
