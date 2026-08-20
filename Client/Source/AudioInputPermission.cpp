/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioInputPermission.h"

void requestAudioInputPermission(AudioInputPermissionCallback callback)
{
	callback(AudioInputPermissionStatus::granted);
}
