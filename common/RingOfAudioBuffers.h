/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

template <class T>
class RingOfAudioBuffers {
public:

	RingOfAudioBuffers(size_t capacity) : endIndex_(0), headIndex_(0)
	{
		data_.resize(capacity);
	}

	bool isEmpty() const
	{
		return endIndex_ == headIndex_;
	}

	void push(std::shared_ptr<T> audioBuffer) {
		// The write pointer always points to the next element to write to
        if (endIndex_ < 0) {
            jassertfalse;
            return;
        }
		data_[(size_t) endIndex_] = audioBuffer;
		incIndex(endIndex_);
		if (endIndex_ == headIndex_) {
			incIndex(headIndex_);
		}
	}

	std::shared_ptr<T> getLast() const
	{
		if (isEmpty()) {
			return std::make_shared<T>();
		}

		int readIndex = endIndex_ - 1;
		if (readIndex < 0) readIndex += (int) data_.size();
		return data_[(size_t) readIndex];
	}

	std::shared_ptr<T> getNthLast(int n) const
	{
		if (isEmpty()) {
			return std::make_shared<T>();
		}

		int readIndex = endIndex_ - 1;
		if (readIndex < 0) readIndex += (int) data_.size();

		// Now move backwards counting to n, but never cross the headIndex_!
		int count = 0;
		while (count < n) {
			if (readIndex == headIndex_) {
				// Can't read past the beginning, return nullptr
				return std::make_shared<T>();
			}
			readIndex -= 1;
			if (readIndex < 0) readIndex += (int) data_.size();
			count++;
		}
		return data_[(size_t) readIndex];
	}

private:
	void incIndex(int &index)
	{
		index = (index + 1) % (int) data_.size();
	}

	std::vector<std::shared_ptr<T>> data_;
	int endIndex_;
	int headIndex_;
};
