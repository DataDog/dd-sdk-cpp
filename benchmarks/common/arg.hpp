// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

/**
 * Name and value strings parsed from a command-line argument ('--name=value').
 */
struct Arg {
  const char* name;
  const char* value;
};

/**
 * Parses a command-line option into name and value, mutating it in-place and returning
 * pointers to the name and value strings, both null-terminator. Requires arguments to
 * be formatted '--foo=bar'. '--foo=' will be interpreted with a value of empty string;
 * '--foo' will be interpreted with an implicit value of '1'. If arg is null or does not
 * match the expected format, returns a result with null values.
 */
Arg ReadArg(char* arg);
