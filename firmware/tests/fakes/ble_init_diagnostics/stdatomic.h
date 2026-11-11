#pragma once
// The real wrapper is C11; the host includes it as C++ to use existing spies.
// Keep actual atomic synchronization in that host build, including task ownership.
#include <atomic>
#define _Atomic(type) std::atomic<type>
#define atomic_load_explicit std::atomic_load_explicit
#define atomic_store_explicit std::atomic_store_explicit
#define atomic_compare_exchange_strong_explicit std::atomic_compare_exchange_strong_explicit
#define memory_order_acquire std::memory_order_acquire
#define memory_order_release std::memory_order_release
#define memory_order_relaxed std::memory_order_relaxed
