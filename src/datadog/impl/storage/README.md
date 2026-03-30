# storage

Code used by the **core** module when dealing with filesystem-based storage.

## Source overview

Lower-level utility code:

- **StoragePath**, **PlatformPath** ([path.hpp](./path.hpp), [.cpp](./path.cpp)): Utility code for representing, manipulating, and encoding filesystem paths in an optimized, platform-agnostic way.
- **IFilesystem** ([filesystem.hpp](./filesystem.hpp), [_posix.cpp](./filesystem_posix.cpp), [_windows.cpp](./filesystem_windows.cpp)): Platform-agnostic interface for examining and manipulating the filesystem, reading from and writing to files, and managing filesystem-level locks.
- [util.hpp](./util.hpp), [.cpp](./util.cpp): Utility code for common path-manipulation and filesystem operations used by the storage interfaces described below.

Higher-level **storage interfaces** for use by different parts of the **Core**:

- **SdkStorage** ([sdk.hpp](./sdk.hpp), [.cpp](./sdk.cpp)): Manages top-level storage directory, handles migration of old processes' data, provides access to specific subdirectories:
    - **ArtifactStorage** ([artifact.hpp](./artifact.hpp), [.cpp](./artifact.cpp)): Manages a directory used to persist arbitrary files across SDK lifetimes (used for crash dumps).
    - **FeatureEventStorage** ([feature_event.hpp](./feature_event.hpp), [.cpp](./feature_event.cpp)): Manages a feature-specific directory which contains `intermediate-v1/` for pending-consent events and `v1/` for consent-granted events.
