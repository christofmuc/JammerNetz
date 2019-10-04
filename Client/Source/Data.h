/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class Data {
public:
	static Data &instance();

	ValueTree &get();

	void initializeFromSettings();
	void saveToSettings();

private:
	Data();

	static Data instance_;

	ValueTree tree_;
};
