/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

#include <vector>

// Fixed-capacity single-producer/single-consumer storage. Slots are allocated
// during construction and are filled in place, so enqueue/dequeue never allocates.
template <typename Item>
class BoundedSpscQueue {
public:
	explicit BoundedSpscQueue(int capacity)
		: fifo_(capacity + 1), slots_(static_cast<size_t>(capacity + 1))
	{
	}

	template <typename Writer>
	bool tryWrite(Writer&& writer)
	{
		int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
		fifo_.prepareToWrite(1, start1, size1, start2, size2);
		juce::ignoreUnused(start2, size2);
		if (size1 == 0) {
			return false;
		}
		writer(slots_[static_cast<size_t>(start1)]);
		fifo_.finishedWrite(1);
		return true;
	}

	template <typename Reader>
	bool tryRead(Reader&& reader)
	{
		int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
		fifo_.prepareToRead(1, start1, size1, start2, size2);
		juce::ignoreUnused(start2, size2);
		if (size1 == 0) {
			return false;
		}
		reader(slots_[static_cast<size_t>(start1)]);
		fifo_.finishedRead(1);
		return true;
	}

	int size() const noexcept { return fifo_.getNumReady(); }
	int freeSpace() const noexcept { return fifo_.getFreeSpace(); }
	void reset() noexcept { fifo_.reset(); }

private:
	juce::AbstractFifo fifo_;
	std::vector<Item> slots_;
};
