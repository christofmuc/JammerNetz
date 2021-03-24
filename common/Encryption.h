/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class UDPEncryption {
public:
	static bool loadKey(String input, std::shared_ptr<MemoryBlock> *outBlock);
	static bool loadKeyFromBase64(String &key, std::shared_ptr<MemoryBlock> *outBlock);
	static bool loadKeyfile(const char *filename, std::shared_ptr<MemoryBlock> *outBlock);
};


