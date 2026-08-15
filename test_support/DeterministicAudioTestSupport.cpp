/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "DeterministicAudioTestSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace jammernetz::test {
namespace {

std::uint64_t mixBits(std::uint64_t value) noexcept
{
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

float missingSample() noexcept
{
	return std::numeric_limits<float>::quiet_NaN();
}

} // namespace

SyntheticAudioSource::SyntheticAudioSource(const std::uint32_t sourceId, const int channelCount,
	const SampleIndex firstSample) :
	sourceId_(sourceId),
	channelCount_(channelCount),
	nextSample_(firstSample)
{
	if (channelCount <= 0) {
		throw std::invalid_argument("A synthetic audio source needs at least one channel");
	}
}

juce::AudioBuffer<float> SyntheticAudioSource::render(const int sampleCount)
{
	if (sampleCount < 0) {
		throw std::invalid_argument("A synthetic audio block cannot have a negative sample count");
	}
	juce::AudioBuffer<float> result(channelCount_, sampleCount);
	renderInto(result, 0, sampleCount);
	return result;
}

void SyntheticAudioSource::renderInto(juce::AudioBuffer<float>& destination,
	const int destinationStartSample,
	const int sampleCount)
{
	if (destinationStartSample < 0 || sampleCount < 0
		|| destinationStartSample + sampleCount > destination.getNumSamples()) {
		throw std::out_of_range("The requested synthetic audio range is outside the destination buffer");
	}
	if (destination.getNumChannels() < channelCount_) {
		throw std::invalid_argument("The destination has fewer channels than the synthetic source");
	}

	destination.clear(destinationStartSample, sampleCount);
	for (int channel = 0; channel < channelCount_; ++channel) {
		float* write = destination.getWritePointer(channel, destinationStartSample);
		for (int sampleOffset = 0; sampleOffset < sampleCount; ++sampleOffset) {
			write[sampleOffset] = valueAt(sourceId_, channel, nextSample_ + static_cast<SampleIndex>(sampleOffset));
		}
	}
	nextSample_ += static_cast<SampleIndex>(sampleCount);
}

std::uint32_t SyntheticAudioSource::sourceId() const noexcept
{
	return sourceId_;
}

int SyntheticAudioSource::channelCount() const noexcept
{
	return channelCount_;
}

SampleIndex SyntheticAudioSource::nextSample() const noexcept
{
	return nextSample_;
}

float SyntheticAudioSource::valueAt(const std::uint32_t sourceId,
	const int channel,
	const SampleIndex absoluteSample)
{
	if (channel < 0) {
		throw std::invalid_argument("A synthetic audio channel cannot be negative");
	}
	std::uint64_t key = absoluteSample;
	key ^= static_cast<std::uint64_t>(sourceId) << 32U;
	constexpr std::uint64_t channelSalt = static_cast<std::uint64_t>(0xd6e8feb86659fd93ULL);
	key ^= static_cast<std::uint64_t>(channel + 1) * channelSalt;
	const std::uint64_t mixed = mixBits(key);
	const auto bucket = static_cast<std::int32_t>((mixed >> 54U) & 0x3ffU) - 512;
	return static_cast<float>(bucket) * (1.0f / 2048.0f);
}

void CapturedAudio::append(const juce::AudioBuffer<float>& source,
	const int sourceStartSample,
	int sampleCount)
{
	if (sampleCount < 0) {
		sampleCount = source.getNumSamples() - sourceStartSample;
	}
	if (sourceStartSample < 0 || sampleCount < 0
		|| sourceStartSample + sampleCount > source.getNumSamples()) {
		throw std::out_of_range("The requested capture range is outside the source buffer");
	}
	if (channels_.empty()) {
		channels_.resize(static_cast<std::size_t>(source.getNumChannels()));
	}
	if (channels_.size() != static_cast<std::size_t>(source.getNumChannels())) {
		throw std::invalid_argument("Captured audio channel count cannot change");
	}

	for (int channel = 0; channel < source.getNumChannels(); ++channel) {
		const float* begin = source.getReadPointer(channel, sourceStartSample);
		auto& destination = channels_[static_cast<std::size_t>(channel)];
		destination.insert(destination.end(), begin, begin + sampleCount);
	}
}

int CapturedAudio::channelCount() const noexcept
{
	return static_cast<int>(channels_.size());
}

std::size_t CapturedAudio::sampleCount() const noexcept
{
	return channels_.empty() ? 0U : channels_.front().size();
}

float CapturedAudio::sample(const int channel, const std::size_t sampleIndex) const
{
	return channels_.at(static_cast<std::size_t>(channel)).at(sampleIndex);
}

const std::vector<std::vector<float>>& CapturedAudio::channels() const noexcept
{
	return channels_;
}

std::vector<DiscrepancySpan> SignalOracle::compare(const CapturedAudio& expected,
	const CapturedAudio& observed,
	const float epsilon,
	const SampleIndex firstSample)
{
	if (epsilon < 0.0f) {
		throw std::invalid_argument("Signal comparison epsilon cannot be negative");
	}

	std::vector<DiscrepancySpan> result;
	const int comparedChannels = std::max(expected.channelCount(), observed.channelCount());
	const std::size_t comparedSamples = std::max(expected.sampleCount(), observed.sampleCount());
	for (int channel = 0; channel < comparedChannels; ++channel) {
		bool spanOpen = false;
		DiscrepancySpan span;
		for (std::size_t sampleIndex = 0; sampleIndex < comparedSamples; ++sampleIndex) {
			const bool expectedExists = channel < expected.channelCount() && sampleIndex < expected.sampleCount();
			const bool observedExists = channel < observed.channelCount() && sampleIndex < observed.sampleCount();
			const float expectedValue = expectedExists ? expected.sample(channel, sampleIndex) : missingSample();
			const float observedValue = observedExists ? observed.sample(channel, sampleIndex) : missingSample();
			const bool samplesAreFinite = expectedExists && observedExists
				&& std::isfinite(expectedValue) && std::isfinite(observedValue);
			const float error = samplesAreFinite
				? std::abs(expectedValue - observedValue)
				: std::numeric_limits<float>::infinity();
			const bool differs = !samplesAreFinite || error > epsilon;

			if (differs && !spanOpen) {
				spanOpen = true;
				span = DiscrepancySpan {
					channel,
					firstSample + static_cast<SampleIndex>(sampleIndex),
					firstSample + static_cast<SampleIndex>(sampleIndex),
					expectedValue,
					observedValue,
					error
				};
			} else if (differs) {
				span.lastSample = firstSample + static_cast<SampleIndex>(sampleIndex);
				span.maximumAbsoluteError = std::max(span.maximumAbsoluteError, error);
			} else if (spanOpen) {
				result.push_back(span);
				spanOpen = false;
			}
		}
		if (spanOpen) {
			result.push_back(span);
		}
	}
	return result;
}

void ScenarioTrace::record(TraceEvent event)
{
	events_.push_back(std::move(event));
}

const std::vector<TraceEvent>& ScenarioTrace::events() const noexcept
{
	return events_;
}

std::string ScenarioTrace::toJsonLines() const
{
	std::string result;
	for (const auto& event : events_) {
		const nlohmann::json document {
			{ "virtual_sample", event.virtualSample },
			{ "sequence", event.sequence },
			{ "kind", event.kind },
			{ "details", event.details }
		};
		result += document.dump();
		result.push_back('\n');
	}
	return result;
}

void ScenarioTrace::writeJsonLines(const juce::File& path) const
{
	const juce::File parent = path.getParentDirectory();
	if (!parent.exists() && parent.createDirectory().failed()) {
		throw std::runtime_error("Could not create scenario trace directory: " + parent.getFullPathName().toStdString());
	}
	auto output = path.createOutputStream();
	if (!output) {
		throw std::runtime_error("Could not open scenario trace for writing: " + path.getFullPathName().toStdString());
	}
	if (!output->setPosition(0)) {
		throw std::runtime_error("Could not seek to the start of scenario trace: "
			+ path.getFullPathName().toStdString());
	}
	if (output->truncate().failed()) {
		throw std::runtime_error("Could not truncate scenario trace: "
			+ path.getFullPathName().toStdString());
	}
	const std::string contents = toJsonLines();
	if (!output->write(contents.data(), contents.size())) {
		throw std::runtime_error("Could not write scenario trace: " + path.getFullPathName().toStdString());
	}
	output->flush();
}

ScenarioScheduler::ScenarioScheduler(const std::uint64_t seed) :
	seed_(seed),
	randomState_(seed)
{
}

std::uint64_t ScenarioScheduler::scheduleAt(const SampleIndex virtualSample,
	std::string kind,
	Action action,
	nlohmann::json details)
{
	if (virtualSample < now_) {
		throw std::invalid_argument("A scenario event cannot be scheduled in the past");
	}
	if (!action) {
		throw std::invalid_argument("A scenario event needs an action");
	}
	const std::uint64_t sequence = nextSequence_++;
	events_.push(ScheduledEvent { virtualSample, sequence, std::move(kind), std::move(action), std::move(details) });
	return sequence;
}

bool ScenarioScheduler::runNext()
{
	if (events_.empty()) {
		return false;
	}
	ScheduledEvent event = events_.top();
	events_.pop();
	now_ = event.virtualSample;
	trace_.record(TraceEvent { event.virtualSample, event.sequence, event.kind, event.details });
	event.action();
	return true;
}

void ScenarioScheduler::runUntilIdle(const std::size_t maximumEvents)
{
	std::size_t processed = 0;
	while (!events_.empty() && processed < maximumEvents) {
		runNext();
		++processed;
	}
	if (!events_.empty()) {
		throw std::runtime_error("Scenario event limit reached before the scheduler became idle");
	}
}

SampleIndex ScenarioScheduler::now() const noexcept
{
	return now_;
}

std::uint64_t ScenarioScheduler::seed() const noexcept
{
	return seed_;
}

std::uint64_t ScenarioScheduler::nextRandom() noexcept
{
	randomState_ += 0x9e3779b97f4a7c15ULL;
	return mixBits(randomState_);
}

bool ScenarioScheduler::empty() const noexcept
{
	return events_.empty();
}

const ScenarioTrace& ScenarioScheduler::trace() const noexcept
{
	return trace_;
}

bool ScenarioScheduler::LaterEvent::operator()(const ScheduledEvent& left,
	const ScheduledEvent& right) const noexcept
{
	if (left.virtualSample != right.virtualSample) {
		return left.virtualSample > right.virtualSample;
	}
	return left.sequence > right.sequence;
}

} // namespace jammernetz::test
