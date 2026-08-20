/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <functional>

enum class AudioInputPermissionStatus {
	granted,
	denied,
	restricted
};

using AudioInputPermissionCallback = std::function<void(AudioInputPermissionStatus)>;

// The callback is delivered on the JUCE message thread. On macOS this may
// display the system microphone consent dialog before invoking the callback.
void requestAudioInputPermission(AudioInputPermissionCallback callback);
