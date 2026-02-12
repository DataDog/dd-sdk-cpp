// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cinttypes>

#include "datadog/impl/storage/path.hpp"

namespace datadog::impl {

#ifdef _WIN32
using PlatformFileHandle = HANDLE;
static const PlatformFileHandle INVALID_FILE_HANDLE = INVALID_HANDLE_VALUE;
#else
using PlatformFileHandle = int;
static const PlatformFileHandle INVALID_FILE_HANDLE = -1;
#endif

enum class FilesystemResult : uint8_t {
  OK,
  AlreadyExistsAsDirectory,
  AlreadyExistsAsFile,
  ParentDirectoryDoesNotExist,
  PermissionDenied,
  ReadOnlyFilesystem,
  OutOfSpace,
  PathTooLong,
  InvalidName,
  UnknownError
};

class IFilesystem {
 public:
  IFilesystem() = default;
  virtual ~IFilesystem() = default;

  // Noncopyable, movable
  IFilesystem(const IFilesystem&) = delete;
  IFilesystem& operator=(const IFilesystem&) = delete;
  IFilesystem(IFilesystem&&) = default;
  IFilesystem& operator=(IFilesystem&&) = default;

  virtual FilesystemResult CreateDirectory(const PlatformPath& path) = 0;
};

}  // namespace datadog::impl
