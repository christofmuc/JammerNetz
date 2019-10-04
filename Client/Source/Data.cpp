/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Data.h"

#include "Settings.h"


Data Data::instance_;

Data & Data::instance()
{
	return instance_;
}

Data::Data() : tree_(Identifier("Setup"))
{
}

juce::ValueTree & Data::get()
{
	return tree_;
}

void Data::initializeFromSettings()
{
	auto xmlDoc = Settings::instance().get("ClientSettings", "");
	auto topLevel = juce::parseXML(xmlDoc);
	if (topLevel) {
		tree_ = ValueTree::fromXml(*topLevel);
	}
}

void Data::saveToSettings()
{
	Settings::instance().set("ClientSettings", tree_.toXmlString().toStdString());
}
