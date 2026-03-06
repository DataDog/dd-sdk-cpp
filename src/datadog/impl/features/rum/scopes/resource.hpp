// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/features/rum/scope.hpp"

namespace datadog::impl {

/**
 * Node in the RUM scope tree modeling a 'Resource', which represents an HTTP request
 * made by the application.
 *
 * All resources are tracked within the context of the View that was active when the
 * request was initiated. A View may have any number of resources active any given time,
 * and the view scope will remain open (after become inactive) until all its child
 * resource scopes are closed.
 *
 * A RUM Resource scope sends exactly one event, and that event is sent when the
 * resource is stopped. A request that receives a valid response (recorded via
 * StopResource) will send a 'resource' event. A request that fails due to an error
 * (recorded via StopResourceWithError) will sent an 'error' event with some basic
 * metadata describing the request.
 *
 * A resource scope only processes `StopResource` commands that explicitly target it by
 * its unique key: no other commands, including `StartResource`, are propagated to the
 * resource scope.
 * TODO(RUM-13166): Detailed RUM metrics may require an `AddResourceMetrics` command,
 * contradicting the above.
 *
 * TODO(RUM-13167): Document trace context injection
 */
class RumResourceScope {
 public:
  enum class Result : uint8_t {
    /** Either resource scope is still open, or it closed without sending an event. */
    SentNoEvent,
    /** Resource scope sent a RUM 'resource' event in response to StopResource. */
    SentResourceEvent,
    /** Resource scope sent a RUM 'error' event in response to StopResourceWithError. */
    SentErrorEvent
  };

  explicit RumResourceScope(
      const RumScopeDependencies& deps,
      class RumViewScope& parent,
      const UUID& resource_id,
      std::string_view key,
      RumResourceMethod method,
      std::string_view url,
      Timestamp start_time,
      const Attribute& attributes
  );

  // Scopes are non-copyable but movable
  ~RumResourceScope() = default;
  RumResourceScope(const RumResourceScope&) = delete;
  RumResourceScope& operator=(const RumResourceScope&) = delete;
  RumResourceScope(RumResourceScope&&) = default;
  RumResourceScope& operator=(RumResourceScope&&) = default;

  // RumScope interface
  RumScopeResult Process(
      const RumCommand& command, const CoreContext& context, const EventWriter& writer
  );

 private:
  /**
   * Generates and sends a RUM 'resource' event in response to the successful completion
   * of a resource (i.e. a StopResource call).
   */
  void SendResourceEvent(
      const RumCommandParams& base,
      const RumStopResourcePayload& payload,
      const CoreContext& context,
      const EventWriter& writer
  );

  /**
   * Generates and sends a RUM 'error' event in response to failure to complete a
   * resource (i.e. a StopResourceWithError call), populating the required fields to
   * describe the resource.
   */
  void SendErrorEvent(
      const RumCommandParams& base,
      const RumStopResourcePayload& payload,
      const RumErrorDetails& error,
      const CoreContext& context,
      const EventWriter& writer
  );

  static UUID ResolveActiveActionId(const RumViewScope& parent);

  Attribute MergeAttributesForEventContext(const RumCommandParams& base);

 private:
  std::reference_wrapper<const RumScopeDependencies> _deps;
  std::reference_wrapper<class RumViewScope> _parent;

  UUID _resource_id;
  std::string _key;
  RumResourceMethod _method;
  std::string _url;
  Timestamp _started_at;
  Attribute _attributes;

  UUID _active_action_id_on_resource_start;

  Result _result{Result::SentNoEvent};

 public:
  UUID GetResourceID() const { return _resource_id; }
  std::string_view GetKey() const { return _key; }
  RumResourceMethod GetMethod() const { return _method; }
  std::string_view GetURL() const { return _url; }
  Result GetResult() const { return _result; }
};

}  // namespace datadog::impl
