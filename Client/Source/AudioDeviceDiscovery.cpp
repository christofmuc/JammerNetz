/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioDeviceDiscovery.h"

#include "Logger.h"

#include "fmt/format.h"
/*
void AudioDeviceDiscovery::listInputChannels(AudioIODevice *device, std::stringstream &list) {
	auto inputs = device->getInputChannelNames();
	for (auto input : inputs) list << "      Input " << input << std::endl;

}

void AudioDeviceDiscovery::listOutputChannels(AudioIODevice *device, std::stringstream &list) {
	auto outputs = device->getOutputChannelNames();
	for (auto output : outputs) list << "      Output " << output << std::endl;
}

void AudioDeviceDiscovery::listBufferSizes(AudioIODevice *device, std::stringstream &list) {
	auto bufferSizes = device->getAvailableBufferSizes();
	for (auto bufferSize : bufferSizes) {
		list << "      Buffer " << bufferSize << std::endl;
	}
	auto sampleRates = device->getAvailableSampleRates();
	for (auto sampleRate : sampleRates) {
		list << "      Sample rate " << sampleRate << std::endl;
	}
}

void AudioDeviceDiscovery::listAudioDevices(AudioDeviceManager &deviceManager, std::stringstream &list)
{
	OwnedArray<AudioIODeviceType> types;
	deviceManager.createAudioDeviceTypes(types);
	for (int i = 0; i < types.size(); ++i)
	{
		String typeName(types[i]->getTypeName());  // This will be things like "DirectSound", "CoreAudio", etc.
		list << "Device type " << typeName << std::endl;
		types[i]->scanForDevices();                 // This must be called before getting the list of devices

		if (types[i]->hasSeparateInputsAndOutputs()) {

			list << "  Inputs:" << std::endl;
			StringArray deviceNames = types[i]->getDeviceNames(true);
			for (auto name : deviceNames) {
				SimpleLogger::instance()->postMessage("Accessing audio device " + name);
				AudioIODevice* device = types[i]->createDevice("", name);
				list << "    Device " << (device != nullptr ? device->getName() : "nullptr") << std::endl;
				listBufferSizes(device, list);
				listInputChannels(device, list);
				delete device;
			}
			list << "  Outputs:" << std::endl;
			deviceNames = types[i]->getDeviceNames(false);
			for (auto name : deviceNames) {
				SimpleLogger::instance()->postMessage("Accessing audio device " + name);
				AudioIODevice* device = types[i]->createDevice(name, name);
				list << "    Device " << (device != nullptr ? device->getName() : "nullptr") << std::endl;
				listBufferSizes(device, list);
				listOutputChannels(device, list);
				delete device;
			}
		}
		else {
			list << "  Input/Outputs:" << std::endl;
			StringArray deviceNames = types[i]->getDeviceNames();
			for (auto name : deviceNames) {
				SimpleLogger::instance()->postMessage("Accessing audio device " + name);
				AudioIODevice* device = types[i]->createDevice(name, name);
				list << "    Device " << (device != nullptr ? device->getName() : "nullptr") << std::endl;
				listBufferSizes(device, list);
				listInputChannels(device, list);
				listOutputChannels(device, list);
				delete device;
			}
		}
	}
}*/

bool AudioDeviceDiscovery::canDeviceDoBufferSize(String const &deviceName, bool isInputDevice, int bufferSize)
{
	return false;
	/*SimpleLogger::instance()->postMessage("Accessing audio device " + deviceName);
	AudioIODevice* device = isInputDevice ? type->createDevice("", deviceName) : type->createDevice(deviceName, "");
	if (device) {
		auto buffers = device->getAvailableBufferSizes();
		delete device;
		for (int buffer : buffers) {
			if (buffer == bufferSize) {
				return true;
			}
		}
	}
	return false;*/
}

bool AudioDeviceDiscovery::canDeviceDoSampleRate(String const &deviceName, bool isInputDevice, int sampleRate)
{
	return false;
	/*SimpleLogger::instance()->postMessage("Accessing audio device " + deviceName);
	AudioIODevice* device = isInputDevice ? type->createDevice("", deviceName) : type->createDevice(deviceName, "");
	if (device) {
		auto rates = device->getAvailableSampleRates();
		delete device;
		for (auto rate : rates) {
			if (((int) std::round(rate)) == sampleRate) {
				return true;
			}
		}
	}
	return false;*/
}

juce::StringArray AudioDeviceDiscovery::allDeviceTypeNames()
{
	init();
	StringArray deviceTypeNames;
	deviceTypeNames.add("miniaudio");
	return deviceTypeNames;
}

/*juce::AudioIODeviceType* AudioDeviceDiscovery::deviceTypeByName(String const& name)
{
	init();
	for (auto device : *deviceTypes_) {
		if (device->getTypeName() == name) {
			return device;
		}
	}
	return nullptr;

}*/

void AudioDeviceDiscovery::shutdown()
{
	ma_context_uninit(&sContext_);
}

void AudioDeviceDiscovery::init()
{
	if (!sIsInitialized) {

		if (ma_context_init(NULL, 0, NULL, &sContext_) != MA_SUCCESS) {
			SimpleLogger::instance()->postMessage("FATAL - cannot initialize miniaudio context!");
			return;
		}

		ma_device_info* pPlaybackDeviceInfos;
		ma_uint32 playbackDeviceCount;
		ma_device_info* pCaptureDeviceInfos;
		ma_uint32 captureDeviceCount;
		ma_uint32 iDevice;

		auto result = ma_context_get_devices(&sContext_, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount);
		if (result != MA_SUCCESS) {
			SimpleLogger::instance()->postMessage("FATAL - cannot retrieve device information from miniaudio context!");
			return;
		}

		for (iDevice = 0; iDevice < playbackDeviceCount; ++iDevice) {
			SimpleLogger::instance()->postMessage(fmt::format("Playback Device ID {}: {}", iDevice, pPlaybackDeviceInfos[iDevice].name));
		}

		for (iDevice = 0; iDevice < captureDeviceCount; ++iDevice) {
			SimpleLogger::instance()->postMessage(fmt::format("Capture Device ID {}: {}", iDevice, pCaptureDeviceInfos[iDevice].name));
			printf("    %u: %s\n", iDevice, pCaptureDeviceInfos[iDevice].name);
		}

		sIsInitialized = true;
	}
}

bool AudioDeviceDiscovery::sIsInitialized = false;

ma_context AudioDeviceDiscovery::sContext_;

