/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "JuceHeader.h"

#include "MainComponent.h"
#include "StreamLogger.h"

#include "version.cpp"

class ClientApplication  : public JUCEApplication
{
public:
    ClientApplication() {}

    const String getApplicationName() override       { return "JammerNetzClient"; }
	const String getApplicationVersion() override { return getClientVersion(); }
    bool moreThanOneInstanceAllowed() override       { return true; }

    void initialise (const String& commandLine) override
    {
		// Parse the command line arguments
		auto list = ArgumentList("Client.exe", commandLine);
		String clientID;
		for (auto arg : list.arguments) {
			if (arg == "--clientID") {
				clientID = arg.getLongOptionValue();
			}
		}

		// Create a file logger. Respect the fact that we might launch multiple clients at the same time, and then we should do collision avoidance with the filenames
		Time now = Time::getCurrentTime();
		int collision = 0;
		std::stringstream name;
		name << "JammerNetzClient" << now.formatted("-%Y-%m-%d-%H-%M-%S") << ".log";
		File wavFile = File::getCurrentWorkingDirectory().getChildFile(name.str());
		while (wavFile.existsAsFile()) {
			collision++;
			name << "JammerNetzClient" << now.formatted("-%Y-%m-%d-%H-%M-%S-") << collision << ".log";
			wavFile = File::getCurrentWorkingDirectory().getChildFile(name.str());
		}
		logger_ = std::make_unique<FileLogger>(wavFile, "Starting "+ getApplicationName() + " V" + getApplicationVersion());
		Logger::setCurrentLogger(logger_.get());

		String windowTitle = getApplicationName();
		if (clientID.isNotEmpty()) {
			windowTitle += ": " + clientID;
		}
        mainWindow = std::make_unique<MainWindow>(windowTitle, clientID);
    }

    void shutdown() override
    {
		StreamLogger::instance().flushBuffer();
		Logger::setCurrentLogger(nullptr);

        // Add your application's shutdown code here..
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public DocumentWindow
    {
    public:
        MainWindow (String name, String clientID)  : DocumentWindow (name,
                                                    Desktop::getInstance().getDefaultLookAndFeel()
                                                                          .findColour (ResizableWindow::backgroundColourId),
                                                    DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(clientID), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
	std::unique_ptr<FileLogger> logger_;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (ClientApplication)
