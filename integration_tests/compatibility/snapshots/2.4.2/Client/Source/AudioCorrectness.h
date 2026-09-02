/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <algorithm>
#include <optional>
#include <vector>

namespace AudioCorrectness {

inline std::optional<int> selectBufferSize(const std::vector<int>& availableSizes, int preferredMinimum = 128)
{
	std::optional<int> smallestAtLeastPreferred;
	std::optional<int> largestAvailable;
	for (const int size : availableSizes) {
		if (size <= 0) {
			continue;
		}

		if (!largestAvailable || size > *largestAvailable) {
			largestAvailable = size;
		}
		if (size >= preferredMinimum && (!smallestAtLeastPreferred || size < *smallestAtLeastPreferred)) {
			smallestAtLeastPreferred = size;
		}
	}

	return smallestAtLeastPreferred ? smallestAtLeastPreferred : largestAvailable;
}

inline bool hasUsableChannelSelection(const std::vector<int>& inputChannels, const std::vector<int>& outputChannels)
{
	return !inputChannels.empty() && !outputChannels.empty();
}

} // namespace AudioCorrectness
