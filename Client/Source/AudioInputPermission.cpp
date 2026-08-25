/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioInputPermission.h"

#include <juce_events/juce_events.h>

#include <utility>

void requestAudioInputPermission(AudioInputPermissionCallback callback)
{
	juce::MessageManager::callAsync([callback = std::move(callback)]() {
		callback(AudioInputPermissionStatus::granted);
	});
}
