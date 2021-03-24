/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Encryption.h"


bool UDPEncryption::loadKey(String input, std::shared_ptr<MemoryBlock> *outBlock)
{
	// Test if this is a UUEncoded string, else assume it is a filename
	if (loadKeyFromBase64(input, outBlock)) {
		return true;
	}
	return loadKeyfile(input.toStdString().c_str(), outBlock);
}

bool UDPEncryption::loadKeyFromBase64(String &key, std::shared_ptr<MemoryBlock> *outBlock)
{
	*outBlock = std::make_shared<MemoryBlock>(72);
	juce::MemoryOutputStream out(**outBlock, false);
	bool valid = Base64::convertFromBase64(out, key);
	out.flush();
	return valid && (*outBlock)->getSize() == 72;
}

bool UDPEncryption::loadKeyfile(const char *filename, std::shared_ptr<MemoryBlock> *outBlock)
{
	File keyFile(filename);
	if (keyFile.existsAsFile()) {
		*outBlock = std::make_shared<MemoryBlock>();
		if (keyFile.loadFileAsData(**outBlock)) {
			return (*outBlock)->getSize() == 72;
		}
	}
	return false;
}

