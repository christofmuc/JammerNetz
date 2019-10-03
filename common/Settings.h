#pragma once

#include "JuceHeader.h"

class Settings {
public:
	static void setSettingsID(String const &id); // Call this before any call to instance
	static Settings & instance();
	void saveAndClose();

	std::string get(std::string const &key, std::string const &default = std::string());
	void set(std::string const &key, std::string const &value);

	File getSessionStorageDir() const;
	
private:
	Settings();

	static ScopedPointer<Settings> instance_;
	static String settingsID_;
	ApplicationProperties properties_;
};
