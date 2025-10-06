// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "datadog/api.hpp"
#include "datadog/uuid.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Rum;
}  // namespace impl

/**
 * Configures the details of the RUM feature upon initialization.
 */
struct RumConfig {
  friend class Rum;
  friend class impl::Rum;

 private:
  UUID application_id;  // UUID::Zero if uninitialized or invalid

 public:
  /**
   * Initializes a new RUM configuration object with all required values.
   *
   * @param in_application_id The ID of your RUM Application. This value can be found
   *  under RUM Applications (https://app.datadoghq.com/rum/list), in the
   *  "SDK Configuration" settings for your Application.
   */
  DATADOG_API explicit RumConfig(std::string_view in_application_id);
  DATADOG_API explicit RumConfig(const UUID& in_application_id);

  // RumConfig is trivially destructible
  ~RumConfig() = default;

  // RumConfig is copyable and movable
  DATADOG_API RumConfig(const RumConfig&) noexcept;
  DATADOG_API RumConfig& operator=(const RumConfig&) noexcept;
  DATADOG_API RumConfig(RumConfig&&) noexcept;
  DATADOG_API RumConfig& operator=(RumConfig&&) noexcept;

  /**
   * Sets the RUM Application ID, overriding the value passed to the constructor.
   */
  DATADOG_API RumConfig& SetApplicationId(std::string_view value);
  DATADOG_API RumConfig& SetApplicationId(const UUID& value);
};

/**
 * Interface to the Datadog SDK's RUM feature.
 */
class Rum {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Rum::Register
  explicit Rum(std::shared_ptr<impl::Rum>&& impl, PrivateCtorTag);
  DATADOG_API ~Rum();

 public:
  /**
   * Registers the RUM feature with the core of the Datadog SDK.
   */
  DATADOG_API static std::shared_ptr<Rum> Register(
      const std::shared_ptr<class Core>& core, const RumConfig& config
  );

 private:
  // Forbid copying/moving: we use std::shared_ptr<Rum> at the API boundary
  Rum(const Rum&) = delete;
  Rum& operator=(const Rum&) = delete;
  Rum(Rum&&) = delete;
  Rum& operator=(Rum&&) = delete;

  std::shared_ptr<impl::Rum> _impl;
};

}  // namespace datadog
