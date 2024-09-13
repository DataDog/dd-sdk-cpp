// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <new>

namespace datadog::internal::allocators {

// RawAllocator allocates memory for STL containers without constructing
// elements.
template <typename T, typename A = std::allocator<T>>
class RawAllocator : public A {
  typedef std::allocator_traits<A> a_t;

 public:
  template <typename U>
  struct rebind {
    using other = RawAllocator<U, typename a_t::template rebind_alloc<U>>;
  };

  template <typename U>
  void construct(U* ptr) noexcept(
      std::is_nothrow_default_constructible<U>::value) {}

  template <typename U, typename... Args>
  void construct(U* ptr, Args&&... args) {
    a_t::construct(static_cast<A&>(*this), ptr, std::forward<Args>(args)...);
  }
};

}  // namespace datadog::internal::allocators
