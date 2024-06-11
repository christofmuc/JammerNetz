#include <juce_audio_devices/juce_audio_devices.h>
#include <combaseapi.h>

using namespace juce;

class MyLogger : public Logger
{
protected:
	virtual void logMessage(const String& message)
	{
		std::cout << message << std::endl;
	}
};

class ASIOAudioIODeviceCallback : public AudioIODeviceCallback
{
public:
	ASIOAudioIODeviceCallback(bool& started) : _started(started)
	{
	}

	void audioDeviceIOCallback(const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override
	{
	}

	void audioDeviceAboutToStart(AudioIODevice* device) override
	{
		_started = true;
	}

	void audioDeviceStopped() override
	{
	}

private:
	bool &_started;
};

int main(int argc, char* argv[])
{
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " ASIO_Device_Name\n";
		return 1;
	}

	CoInitialize(nullptr);

	ConsoleApplication app;

	ConsoleApplication::Command command;

	command.command = ([](const auto& args) {
		String asioDeviceName = args.arguments[0].text;

		AudioDeviceManager audioDeviceManager;
		audioDeviceManager.initialiseWithDefaultDevices(0, 2); // Stereo output

		AudioIODeviceType* asioDeviceType = nullptr;
		for (auto deviceType : audioDeviceManager.getAvailableDeviceTypes()) {
			if (deviceType->getTypeName() == "ASIO") {
				asioDeviceType = deviceType;
			}
		}

		if (asioDeviceType == nullptr) {
			std::cerr << "ASIO not available!\n";
			return;
		}

		StringArray asioDeviceNames = asioDeviceType->getDeviceNames();
		int asioDeviceIndex = asioDeviceNames.indexOf(asioDeviceName);

		if (asioDeviceIndex == -1) {
			std::cerr << "ASIO device '" << asioDeviceName << "' not found!\n";
			for (auto name : asioDeviceNames) {
				std::cerr << name << std::endl;
			}
			return;
		}

		AudioIODevice* asioDevice = asioDeviceType->createDevice(asioDeviceNames[asioDeviceIndex], String());
		if (asioDevice == nullptr) {
			std::cerr << "Failed to create ASIO device!\n";
			return;
		}

		BigInteger inputChannelMask;
		inputChannelMask.setRange(1, 1, true);

		String openMessage = asioDevice->open(inputChannelMask, inputChannelMask, 48000.0, 128);
		if (openMessage.isNotEmpty()) {
			std::cerr << "Failed to open ASIO device: " << openMessage.toStdString() << std::endl;
			return;
		}

		bool started = false;
		asioDevice->start(new ASIOAudioIODeviceCallback(started));

		// Wait 5 seconds
		Thread::sleep(5000);

		if (started) {
			std::cout << "ASIO device started successfully!\n";
		} else {
			std::cout << "Failed to start ASIO device!\n";
		}

		return;
	});
	app.addDefaultCommand(command);

	auto mm = MessageManager::getInstance();
	juce::Logger::setCurrentLogger(new MyLogger());
	initialiseJuce_GUI();
	app.findAndRunCommand(argc, argv);
}
