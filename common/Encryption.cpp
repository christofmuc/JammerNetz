/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Encryption.h"

#include "Logger.h"

bool UDPEncryption::loadKeyfile(const char *filename, std::shared_ptr<MemoryBlock> *outBlock)
{
	File keyFile(filename);
	if (keyFile.existsAsFile()) {
		*outBlock = std::make_shared<MemoryBlock>();
		if (keyFile.loadFileAsData(**outBlock)) {
			juce::MD5 md5(**outBlock);
			std::string fingerprint = md5.toHexString().toStdString();
			SimpleLogger::instance()->postMessage("New key loaded with fingerprint " + fingerprint);
			return true;
		}
	}
	return false;
}

