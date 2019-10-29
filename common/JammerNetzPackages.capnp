#
#   Copyright (c) 2019 Christof Ruch. All rights reserved.
#
#   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
#

@0xad8d98932796d9d2;

struct JammerNetzPNPStreamQualityInfo {
	# Unhealed problems
	tooLateOrDuplicate @0 :UInt64;
	droppedPacketCounter @1 :Int64;

	# Healed problems
	outOfOrderPacketCounter @2 :Int64;
	duplicatePacketCounter @3 :Int64;
	dropsHealed @4 :UInt64;

	# Pure statistics
	packagesPushed @5 :UInt64;
	packagesPopped @6 :UInt64;
	maxLengthOfGap @7 :UInt64;
	maxWrongOrderSpan @8 :UInt64;
}

struct JammerNetzPNPClientInfo {
	ipAddress @0 :Data;  # The whole V6 IP address data. IP V4 would only use the first 4 bytes, V6 needs 16 bytes
	isIPV6 @1 :Bool; 
	portNumber @2 :Int16;
	qualityInfo @3 :JammerNetzPNPStreamQualityInfo;
}

struct JammerNetzPNPClientInfoPackage {
	clientInfos @0 :List(JammerNetzPNPClientInfo);
}
