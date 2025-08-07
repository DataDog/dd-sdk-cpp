#!/usr/bin/env bash
# Helper script that wraps CMake builds, both for convenience when iterating locally,
# and to facilitate deterministic builds against specific toolchains in CI.
#
# To perform an incremental build of the project, with examples/tests/linting/etc.:
# - ./build.sh
#
# To use a specific version of clang, and perform a clean rebuild with new CMake config:
# - ./build.sh --clang-version 20 --rebuild
#
set -euo pipefail

DEFAULT_VISUAL_STUDIO_VERSION="Visual Studio 17 2022"

# Utility for checking if an executable is in the PATH, without tripping -e
resolve_binary() {
    local BINARY_NAME="$1"
    set +e
    local BINARY_PATH
    BINARY_PATH=$(command -v "$BINARY_NAME" 2>/dev/null)
    set -e
    echo "$BINARY_PATH"
}

find_clang_binaries() {
    local CLANG_PATHS=""
    IFS=':' read -ra PATH_DIRS <<< "$PATH"
    for DIR in "${PATH_DIRS[@]}"; do
        if [ -n "$DIR" ] && [ -d "$DIR" ]; then
            local CLANG_BINARY="$DIR/clang"
            if [ -x "$CLANG_BINARY" ]; then
                if [ -n "$CLANG_PATHS" ]; then
                    CLANG_PATHS="$CLANG_PATHS $CLANG_BINARY"
                else
                    CLANG_PATHS="$CLANG_BINARY"
                fi
            fi
        fi
    done
    echo "$CLANG_PATHS"
}

resolve_clang() {
    local TARGET_VERSION="$1"

    # Find all clang binaries in PATH
    set +e
    local CLANG_PATHS
    CLANG_PATHS=$(find_clang_binaries)
    set -e
    
    for CLANG_NAME in $CLANG_PATHS; do
        local CLANG_PATH
        CLANG_PATH=$(command -v "$CLANG_NAME" 2>/dev/null)

        if [ -n "$CLANG_PATH" ]; then
            # Extract version using the same command as line 26
            local version
            VERSION=$("$CLANG_PATH" --version | head -n 1 | sed -nE 's/.*clang version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')

            # Check if version matches the given prefix
            if [ -n "$VERSION" ] && [[ "$VERSION" == "$TARGET_VERSION"* ]]; then
                echo "$CLANG_PATH"
                return 0
            fi
        fi
    done

    # No matching clang found
    return 1
}

# Verify that cmake is installed
if [ -z "$(resolve_binary cmake)" ]; then
    echo "ERROR: cmake is not installed or not in PATH" >&2
    exit 1
fi

# Parse args: user can optionally override defaults
REBUILD=""
PLATFORM=""
GENERATOR=""
COMPILER=""
CONFIG="debug"
REQUIRED_CLANG_VERSION=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --rebuild)
            REBUILD="1"
            shift
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --compiler)
            COMPILER="$2"
            shift 2
            ;;
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --clang-version)
            REQUIRED_CLANG_VERSION="$2"
            shift 2
            ;;
        *)
            echo "ERROR: Unknown option '$1'" >&2
            exit 1
            ;;
    esac
done

# Validate --config argument
if [ "$CONFIG" != "debug" ] && [ "$CONFIG" != "release" ]; then
    echo "ERROR: Unrecognized config '$CONFIG'" >&2
    exit 1
fi

# Auto-detect platform if not set
if [ "$PLATFORM" == "" ]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PLATFORM="Darwin"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        PLATFORM="Linux"
    elif [ "$OSTYPE" == "msys" ] || [ "$OSTYPE" == "cygwin" ] || [ -f /proc/version && $(grep -i microsoft /proc/version) ]; then
        PLATFORM="Windows"
    fi
fi
if [ "$PLATFORM" == "" ]; then
    echo "ERROR: Unable to detect platform (OSTYPE=$OSTYPE)" >&2
    exit 1
fi

# Resolve default build tools based on platform and PATH
if [ "$GENERATOR" == "" ]; then
    if [ "$PLATFORM" == "Darwin" ] || [ "$PLATFORM" == "Linux" ]; then
        # Default to make, but use ninja if installed
        GENERATOR="Unix Makefiles"
        if [ -n "$(resolve_binary ninja)" ]; then
            GENERATOR="Ninja"
        fi

        # Compile using clang, unless another compiler was specified
        if [ "$COMPILER" == "" ]; then
            COMPILER="clang"
        fi
    elif [ "$PLATFORM" == "Windows" ]; then
        # Use Visual Studio; don't explicitly configure MSVC for now
        GENERATOR="$DEFAULT_VISUAL_STUDIO_VERSION"
    fi
fi
if [ "$GENERATOR" == "" ]; then
    echo "ERROR: Failed to resolve default CMake generator (PLATFORM=$PLATFORM)" >&2
    exit 1
fi

# Start figuring out cmake configure options
CMAKE_ARGS=("-G" "$GENERATOR")
if [ "$CONFIG" == "debug" ]; then
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Debug")
else
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Release")
fi
CMAKE_ARGS+=("-DDD_ENABLE_CLANG_TIDY=ON")
CMAKE_ARGS+=("-DDD_BUILD_EXAMPLES=ON")
CMAKE_ARGS+=("-DDD_BUILD_TESTING=ON")

# If using clang with a required version, resolve that version and specify it explicitly
if [ "$COMPILER" == "clang" ] && [ "$REQUIRED_CLANG_VERSION" != "" ]; then
    CLANG_BINARY_PATH=$(resolve_clang "$REQUIRED_CLANG_VERSION")
    if [ "$CLANG_BINARY_PATH" == "" ]; then
        echo "ERROR: Could not resolve an installed version of Clang $REQUIRED_CLANG_VERSION" >&2
        exit 1
    fi
    CMAKE_ARGS+=("-DCMAKE_C_COMPILER=$CLANG_BINARY_PATH")
    CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=$CLANG_BINARY_PATH++")
fi

# If --rebuild was passed, nuke any existing build directory first
if [ "$REBUILD" != "" ]; then
    # Sanity check: only auto-delete if it's empty or looks like a CMake build dir,
    # unless running in CI
    if [ "${CI:-}" != "" ] || [ -f build/CMakeCache.txt ]; then
        rm -rf build
    elif [ -d build ]; then
        rmdir build
    fi
fi

# Configure the CMake project and invoke a build
mkdir -p build
cmake -S . -B build "${CMAKE_ARGS[@]}"
cmake --build build
