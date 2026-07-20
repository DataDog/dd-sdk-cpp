#!/bin/sh
# Run the unit test suite inside the dd-sdk-cpp/i386 container.
# Invoked by: docker run --rm --platform linux/386 -v "$(pwd):/src" -w /src dd-sdk-cpp/i386 sh ci/i386/run-tests.sh
set -e

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDD_ENABLE_GIT_REVISION_INFO=OFF \
    -DDD_ENABLE_CLANG_FORMAT=OFF \
    -DDD_ENABLE_CLANG_TIDY=OFF \
    -DDD_ENABLE_COVERAGE=OFF \
    -DDD_ENABLE_SANITIZERS= \
    -DDD_ENABLE_ASSERTS=ON \
    -DDD_BUILD_TESTING=ON \
    -DDD_HTTP_USE_SYSTEM_LIBCURL=ON \
    -S . -B build-i386

cmake --build build-i386

ctest --test-dir build-i386 --output-on-failure
