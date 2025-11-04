// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>
#include <string>
#include <string_view>

#include "datadog/api.hpp"
#include "datadog/attribute.hpp"
#include "datadog/uuid.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Rum;
struct RumScopeDependencies;
}  // namespace impl

/**
 * Configures the details of the RUM feature upon initialization.
 */
struct RumConfig {
  friend class Rum;
  friend class impl::Rum;
  friend struct impl::RumScopeDependencies;

 private:
  UUID application_id;  // UUID::Zero if uninitialized or invalid
  float session_sample_rate{100.0f};

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

  /**
   * Sets the sample rate to a value between 0.0 and 100.0, indicating what percentage
   * of RUM sessions should be sampled. At 100.0, events for all sessions are sent to
   * intake; at 0.0, no RUM events are generated. Default is 100.0.
   */
  DATADOG_API RumConfig& SetSessionSampleRate(float value);
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

  /**
   * Adds or updates a global attribute value that will be included with all RUM events
   * emitted hereafter.
   */
  DATADOG_API void AddAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes a global attribute value, if one has been previously added with the given
   * name.
   */
  DATADOG_API void RemoveAttribute(std::string_view name);

  /**
   * Explicitly stops the current RUM session, if one is active.
   *
   * Once a session has been explicitly stopped, the next call to StartView(),
   * StartAction(), or AddAction() will automatically start a new session. If that new
   * session is created in response to an action, the last active view from the previous
   * session will be restarted in the new session.
   */
  DATADOG_API void StopSession();

  /**
   * Starts a new RUM view, recording that the user has navigated to the portion of the
   * application uniquely identified by the given string key.
   *
   * If no RUM session is currently active, starting a view will implicitly create a new
   * session.
   *
   * When a new view is started, all existing views are implicitly stopped.
   *
   * @param name An optional human-readable view name to be used in the Datadog UI. If
   *  not specified, defaults to the value of `key`.
   * @param attributes An optional set of custom attributes to associate with the view.
   */
  DATADOG_API void StartView(
      std::string_view key,
      std::string_view name = std::string_view{},
      const Attribute& attributes = Attribute()
  );

  /**
   * Adds or updates a custom attribute value stored in the context of the current view,
   * provided that the current view has the given key.
   *
   * All events produced within the context of a view will include the set of custom
   * attributes formed from both global and view-level attributes, with view attributes
   * taking precedence in the case of name conflicts.
   *
   * View attributes are scoped to the lifetime of the view and do not persist to
   * subsequent views with the same key.
   */
  DATADOG_API void AddViewAttribute(
      std::string_view view_key, std::string_view attribute_name, const Attribute& value
  );

  /**
   * Removes any custom attribute value stored under the given name in the context of
   * the current view, provided that the current view has the given key.
   *
   * If the view-level attribute being removed was shadowing a global attribute of the
   * same name, subsequent events will once again use the global attribute value.
   */
  DATADOG_API void RemoveViewAttribute(
      std::string_view view_key, std::string_view attribute_name
  );

  /**
   * Stops any active RUM views that are identified with the given key.
   */
  DATADOG_API void StopView(
      std::string_view key, const Attribute& attributes = Attribute()
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
