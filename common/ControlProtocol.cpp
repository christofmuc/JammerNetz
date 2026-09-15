/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ControlProtocol.h"

bool JammerNetzControlEnvelopeData::isStructurallyValid() const
{
	if (protocolVersion != JammerNetzControlProtocol::Current
		|| messageId == 0
		|| topic.empty()
		|| topic.size() > JammerNetzControlProtocol::MaximumTopicBytes) {
		return false;
	}

	if (payload.dump().size() > JammerNetzControlProtocol::MaximumPayloadBytes) {
		return false;
	}
	if (route != JammerNetzControlRoute::Server
		&& route != JammerNetzControlRoute::Unicast
		&& route != JammerNetzControlRoute::Broadcast) {
		return false;
	}
	if (delivery != JammerNetzControlDelivery::Ephemeral
		&& delivery != JammerNetzControlDelivery::Acknowledged
		&& delivery != JammerNetzControlDelivery::Retained) {
		return false;
	}

	if (route == JammerNetzControlRoute::Unicast && targetId == 0) {
		return false;
	}
	if (route != JammerNetzControlRoute::Unicast && targetId != 0) {
		return false;
	}
	return true;
}
