// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <random>
#include <string>

#include "features/rum/scope.hpp"
#include "features/rum/scopes/session.hpp"

namespace datadog::impl {

/**
 * Top-level node in the tree of RUM scopes that model the current state of the user's
 * interactions with the application.
 *
 * The RumApplicationScope represents the application as a whole, and as such it is
 * always present while RUM is enabled. The application scope is responsible for
 * creating and managing the lifetime of child RumSessionScopes.
 */
class RumApplicationScope {
 public:
  explicit RumApplicationScope(const RumScopeDependencies& deps);

  // RumContextProvider interface
  void PopulateContext(struct RumContext& out_context) const;

  // RumScope interface
  RumScopeResult Process(const RumCommand& command);

 private:
  /**
   * Starts the first session for our application, in response to an initial command.
   */
  RumSessionScope CreateInitialSession(const RumCommand& command);

 private:
  // Non-owning reference to the immutable set of input values injected into all scopes
  std::reference_wrapper<const RumScopeDependencies> _deps;

  // Child sessions
  std::optional<RumSessionScope> _active_session;
  std::optional<RumSessionScope> _prev_session;

 public:
  ScopeRef<const RumSessionScope> GetActiveSession() const;
  ScopeRef<const RumSessionScope> GetMostRecentSession() const;
};

}  // namespace datadog::impl
