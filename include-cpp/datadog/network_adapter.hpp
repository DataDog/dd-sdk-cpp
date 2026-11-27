// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/api.hpp"

namespace datadog {

class Core;

namespace impl {
class NetworkAdapter;
}  // namespace impl

/**
 * Public handle that owns a reference to a network adapter implementation.
 */
class NetworkAdapter {
 private:
  struct PrivateCtorTag {};

 public:
  explicit NetworkAdapter(PrivateCtorTag);
  explicit NetworkAdapter(
      const std::shared_ptr<impl::NetworkAdapter>& impl, PrivateCtorTag
  );

  /** Releases this handle's reference to the implementation. */
  DATADOG_API ~NetworkAdapter();

 private:
  NetworkAdapter(const NetworkAdapter&) = delete;
  NetworkAdapter& operator=(const NetworkAdapter&) = delete;
  NetworkAdapter(NetworkAdapter&&) = delete;
  NetworkAdapter& operator=(NetworkAdapter&&) = delete;

  std::shared_ptr<impl::NetworkAdapter> _impl;

  friend class Core;
};

}  // namespace datadog
