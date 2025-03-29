/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JammerNetzPackage.h"

#include <vector>

/*
  | CLIENTINFO type message - these are sent only by the server to the clients
  | JammerNetzClientInfoMessage
  | JammerNetzHeader | [Array of JammerNetzClientInfo]                    |
  |                  | ipAdress isIPV6 Port JammerNetzStreamQualityInfo   |
*/

struct JammerNetzStreamQualityInfo {
	// Unhealed problems
	uint64_t tooLateOrDuplicate{};
	int64_t droppedPacketCounter{};

	// Healed problems
	int64_t outOfOrderPacketCounter{};
	int64_t duplicatePacketCounter{};
	uint64_t dropsHealed{};

	// Pure statistics
	uint64_t packagesPushed{};
	uint64_t packagesPopped{};
	uint64_t maxLengthOfGap{};
	uint64_t maxWrongOrderSpan{};

	double wallClockDelta{};
	double jitterMeanMillis{};
	double jitterSDMillis{};
};

struct JammerNetzClientInfo {
	JammerNetzClientInfo(IPAddress ipAddress, int portNumber, JammerNetzStreamQualityInfo qualityInfo);

	uint8 ipAddress[16]; // The whole V6 IP address data. IP V4 would only use the first 4 bytes
	bool isIPV6; // Not sure if I need this
	int portNumber;
	JammerNetzStreamQualityInfo qualityInfo;
};

class JammerNetzClientInfoMessage : public JammerNetzMessage {
public:
	JammerNetzClientInfoMessage();
	JammerNetzClientInfoMessage(JammerNetzClientInfoMessage const &other) = default;
	void addClientInfo(IPAddress ipAddress, int port, JammerNetzStreamQualityInfo infoData);

	virtual MessageType getType() const override;

	uint8 getNumClients() const;
	String getIPAddress(uint8 clientNo) const;
	JammerNetzStreamQualityInfo getStreamQuality(uint8 clientNo) const;

	// Deserializing constructor, used by JammerNetzMessage::deserialize()
	JammerNetzClientInfoMessage(uint8 *data, size_t bytes);

	// Implementing the serialization interface
	virtual void serialize(uint8 *output, size_t &byteswritten) const override;

private:
	std::vector<JammerNetzClientInfo> clientInfos_;
};
