/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <limits>
#include <stdexcept>

inline bool sizet_is_safe_as_int(size_t size) {
	return size <= static_cast<size_t>(std::numeric_limits<int>::max());
}

inline int safe_sizet_to_int(size_t size) {
	if (sizet_is_safe_as_int(size)) {
		return static_cast<int>(size);
	}
	throw std::runtime_error("size_t out of range for int");
}

inline size_t safe_int_to_sizet(int integer) {
	if (integer < 0)
		throw std::runtime_error("int out of range for size_t");
	return static_cast<size_t>(integer);
}
