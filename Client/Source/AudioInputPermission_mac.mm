/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "AudioInputPermission.h"

#include <juce_events/juce_events.h>

#import <AVFoundation/AVFoundation.h>

#include <memory>
#include <utility>

namespace {
void deliverPermissionResult(const std::shared_ptr<AudioInputPermissionCallback>& callback, AudioInputPermissionStatus status)
{
	juce::MessageManager::callAsync([callback, status]() {
		(*callback)(status);
	});
}
}

void requestAudioInputPermission(AudioInputPermissionCallback callback)
{
	auto sharedCallback = std::make_shared<AudioInputPermissionCallback>(std::move(callback));
	const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

	switch (status) {
	case AVAuthorizationStatusAuthorized:
		deliverPermissionResult(sharedCallback, AudioInputPermissionStatus::granted);
		break;
	case AVAuthorizationStatusRestricted:
		deliverPermissionResult(sharedCallback, AudioInputPermissionStatus::restricted);
		break;
	case AVAuthorizationStatusDenied:
		deliverPermissionResult(sharedCallback, AudioInputPermissionStatus::denied);
		break;
	case AVAuthorizationStatusNotDetermined:
		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
			completionHandler:^(BOOL granted) {
				deliverPermissionResult(sharedCallback,
					granted ? AudioInputPermissionStatus::granted : AudioInputPermissionStatus::denied);
			}];
		break;
	default:
		deliverPermissionResult(sharedCallback, AudioInputPermissionStatus::restricted);
		break;
	}
}
