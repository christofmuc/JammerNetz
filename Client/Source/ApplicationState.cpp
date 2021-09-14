#include "ApplicationState.h"

#include "Settings.h"

juce::ValueTree ApplicationState::sApplicationState("ApplicationState");

void ApplicationState::fromSettings()
{
	//TODO This could be simpler!
	auto userName = Settings::instance().get("username", SystemStats::getFullUserName().toStdString());
	sApplicationState.setProperty(VALUE_USER_NAME, String(userName), nullptr);
}

void ApplicationState::toSettings()
{
		//TODO This should not be necessary to be called explicitly, instead it could always write all settings when one is changed
	auto username = ApplicationState::sApplicationState.getProperty(VALUE_USER_NAME);
	Settings::instance().set("username", username.toString().toStdString());
}

