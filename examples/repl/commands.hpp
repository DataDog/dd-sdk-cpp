// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "repl/command.hpp"

struct State;

// util
CommandResult HandleSource(State& state, const CommandInput& args);
CommandResult HandleSleep(State& state, const CommandInput& args);
CommandResult HandleUrl(State& state, const CommandInput& args);
CommandResult HandleNop(State& state, const CommandInput& args);

// profile
CommandResult HandleStartProfile(State& state, const CommandInput& args);
CommandResult HandleStopProfile(State& state, const CommandInput& args);

// config
CommandResult HandleSetConfig(State& state, const CommandInput& args);

// core
CommandResult HandleCreateCore(State& state, const CommandInput& args);
CommandResult HandleResetCore(State& state, const CommandInput& args);
CommandResult HandleSetTrackingConsent(State& state, const CommandInput& args);
CommandResult HandleSetUserInfo(State& state, const CommandInput& args);
CommandResult HandleAddUserExtraInfo(State& state, const CommandInput& args);
CommandResult HandleClearUserInfo(State& state, const CommandInput& args);
CommandResult HandleStartCore(State& state, const CommandInput& args);
CommandResult HandleStopCore(State& state, const CommandInput& args);

// logging
CommandResult HandleRegisterLogging(State& state, const CommandInput& args);
CommandResult HandleCreateLogger(State& state, const CommandInput& args);
CommandResult HandleLog(State& state, const CommandInput& args);

// rum
CommandResult HandleRegisterRum(State& state, const CommandInput& args);
CommandResult HandleStopSession(State& state, const CommandInput& args);
CommandResult HandleStartView(State& state, const CommandInput& args);
CommandResult HandleStopView(State& state, const CommandInput& args);
CommandResult HandleAddAction(State& state, const CommandInput& args);
CommandResult HandleStartAction(State& state, const CommandInput& args);
CommandResult HandleStopAction(State& state, const CommandInput& args);
CommandResult HandleStartResource(State& state, const CommandInput& args);
CommandResult HandleStopResource(State& state, const CommandInput& args);
CommandResult HandleStopResourceWithError(State& state, const CommandInput& args);
CommandResult HandleAddError(State& state, const CommandInput& args);
CommandResult HandleStartOperation(State& state, const CommandInput& args);
CommandResult HandleSucceedOperation(State& state, const CommandInput& args);
CommandResult HandleFailOperation(State& state, const CommandInput& args);

// crash
CommandResult HandleRegisterCrashReporting(State& state, const CommandInput& args);
CommandResult HandleCrash(State& state, const CommandInput& args);
