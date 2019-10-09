#include "Settings.h"

#include "StreamLogger.h"

juce::String Settings::settingsID_ = "JammerNetz"; // This can be overridden for testing, so you can start more than one client on a single machine and don't overwrite your settings file

Settings & Settings::instance()
{
	static Settings instance_;
	return instance_;
}

void Settings::setSettingsID(String const &id)
{
	settingsID_ = id;
}

std::string Settings::get(std::string const &key, std::string const &default) 
{
	return properties_.getUserSettings()->getValue(key, default).toStdString();
}

void Settings::set(std::string const &key, std::string const &value)
{
	properties_.getUserSettings()->setValue(key, String(value));
}

File Settings::getSessionStorageDir() const
{
	auto dir = File(File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName() + "/JammerNetzSession");
	if (!dir.exists()) {
		dir.createDirectory();
	}
	else {
		if (dir.existsAsFile()) {
			jassert(false);
			StreamLogger::instance() << "Can't record to " << dir.getFullPathName() << " as it is a file and not a directory!" << std::endl;
			return File::getSpecialLocation(File::userDocumentsDirectory).getFullPathName();
		}
	}
	return dir;
}

Settings::Settings()
{
	PropertiesFile::Options options;
	options.folderName = "JammerNetz";
	options.applicationName = settingsID_;
	options.filenameSuffix = ".settings";
	options.commonToAllUsers = false;
	properties_.setStorageParameters(options);
}

void Settings::saveAndClose()
{
	properties_.closeFiles();
}

