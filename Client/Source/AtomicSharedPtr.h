/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <atomic>
#include <memory>

template <typename T>
class AtomicSharedPtr {
public:
	AtomicSharedPtr() = default;

	void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst)
	{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
		value_.store(std::move(value), order);
#else
		std::atomic_store_explicit(&value_, std::move(value), order);
#endif
	}

	[[nodiscard]] std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const
	{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
		return value_.load(order);
#else
		return std::atomic_load_explicit(&value_, order);
#endif
	}

private:
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
	std::atomic<std::shared_ptr<T>> value_;
#else
	std::shared_ptr<T> value_;
#endif
};
