#pragma once

#include "JuceHeader.h"

class Settings {
public:
	static void setSettingsID(String const &id); // Call this before any call to instance
	static Settings & instance();
	void saveAndClose();

	std::string get(std::string const &key, std::string const &defaultValue = std::string());
	void set(std::string const &key, std::string const &value);

	File getSessionStorageDir() const;

	// Singleton!
	Settings(Settings const &) = delete;
	void operator=(Settings const&) = delete;

private:
	Settings();

	static String settingsID_;
	ApplicationProperties properties_;
};
