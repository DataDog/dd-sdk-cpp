# Dockerfile for validating dd-sdk-cpp builds and unit tests on 32-bit x86 (i686).
#
# This image is not used in CI (which runs on x86_64/arm64). Its purpose is local
# validation of 32-bit correctness on a macOS arm64 host, via QEMU user-space emulation
# provided by Docker's linux/386 platform support.
#
# Build:
#   docker buildx build --platform linux/386 -t dd-sdk-cpp/i386 ci/i386
#
# Run unit tests (from the repo root):
#   docker run --rm --platform linux/386 \
#     -v "$(pwd):/src" -w /src \
#     dd-sdk-cpp/i386 \
#     sh ci/i386/run-tests.sh

FROM --platform=linux/386 alpine:3.21

# Build tools
RUN apk add --no-cache \
    cmake \
    make \
    gcc \
    g++ \
    # uuid/uuid.h and libuuid, required on Linux for UUID generation
    util-linux-dev \
    # libcurl, used as the HTTP client backend
    curl-dev \
    # OpenSSL, required by libcurl
    openssl-dev \
    # zlib, required by libcurl
    zlib-dev \
    # Linux kernel headers, required by the crash handler
    linux-headers \
    # git, used by cmake/git-revision-info.cmake at configure time
    git
