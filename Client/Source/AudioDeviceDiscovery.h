/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include "miniaudio.h"

class AudioDeviceDiscovery {
public:
	/*static void listInputChannels(AudioIODevice *device, std::stringstream &list);
	static void listOutputChannels(AudioIODevice *device, std::stringstream &list);
	static void listBufferSizes(AudioIODevice *device, std::stringstream &list);

	static void listAudioDevices(AudioDeviceManager &deviceManager, std::stringstream &list);*/

	static bool canDeviceDoBufferSize(String const &deviceName, bool isInputDevice, int bufferSize);
	static bool canDeviceDoSampleRate(String const &deviceName, bool isInputDevice, int sampleRate);

	static StringArray allDeviceTypeNames();
	//static AudioIODeviceType* deviceTypeByName(String const& name);

	static void shutdown();

private:
	static void init();
	static bool sIsInitialized;
	static ma_context sContext_;
};