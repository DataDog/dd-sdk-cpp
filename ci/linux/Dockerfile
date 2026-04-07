# This Dockerfile describes the build environment used for Linux builds of dd-sdk-cpp,
# with various toolchain versions defined using multi-stage targets
FROM ubuntu:22.04 AS base

# Suppress prompts; default to UTC
ENV DEBIAN_FRONTEND=noninteractive
RUN ln -sf /usr/share/zoneinfo/Etc/UTC /etc/localtime

# Install required tools and libraries
# - ca-certificates, gpg, gpg-agent, wget: For third-party apt repositories
# - libcurl4-openssl-dev, libssl-dev:      For default libcurl HTTP client impl
# - zlib1g-dev:                            Required by crashpad (along with libcurl)
# - tzdata:                                Required for crashpad TimeZone tests
# - uuid-dev:                              uuid/uuid.h, for datadog::UUID on Linux
# - git:                                   Used by various scripts
# - ninja-build:                           Build system for use as CMake generator
RUN apt-get update && apt-get install -y --no-install-recommends \
  software-properties-common \
  ca-certificates \
  gpg \
  gpg-agent \
  wget \
  libcurl4-openssl-dev \
  libssl-dev \
  zlib1g-dev \
  tzdata \
  uuid-dev \
  git \
  ninja-build

# Install Python 3.12 (not available in stock Ubuntu repos as of 22.04), for scripts
# declared in pyproject.toml, and install it as the system's default Python interpreter
RUN add-apt-repository ppa:deadsnakes/ppa \
  && apt update \
  && apt install --no-install-recommends -y python3.12 python3.12-venv \
  && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 1

# Use Ninja by default for CMake builds on Linux: it's ~6x faster than Make
ENV CMAKE_GENERATOR=Ninja

# Install CMake from Kitware repo to get the latest version
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy-rc main' | tee -a /etc/apt/sources.list.d/kitware.list >/dev/null \
  && apt-get update \
  && apt-get install -y --no-install-recommends cmake

# Define the various compiler toolchains we want to build with using Docker

# Build `ci/dd-sdk-cpp/gcc11` with: --target gcc11
FROM base AS gcc11
RUN add-apt-repository ppa:ubuntu-toolchain-r/test \
  && apt-get update \
  && apt-get install -y --no-install-recommends gcc-11 g++-11
ENV CC=gcc-11
ENV CXX=g++-11

# Build `ci/dd-sdk-cpp/gcc13` with: --target gcc13
FROM base AS gcc13
RUN add-apt-repository ppa:ubuntu-toolchain-r/test \
  && apt-get update \
  && apt-get install -y --no-install-recommends gcc-13 g++-13
ENV CC=gcc-13
ENV CXX=g++-13

# Build `ci/dd-sdk-cpp/clang15` with: --target clang15
FROM base AS clang15
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
  && echo 'deb https://apt.llvm.org/jammy/ llvm-toolchain-jammy-15 main' | tee -a /etc/apt/sources.list.d/llvm.list \
  && apt-get update \
  && apt-get install -y --no-install-recommends clang-15 clang-format-15 clang-tidy-15
ENV PATH="/usr/lib/llvm-15/bin:${PATH}"
ENV CC=clang-15
ENV CXX=clang++-15

# Build `ci/dd-sdk-cpp/clang20` with: --target clang20
FROM base AS clang20
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
  && echo 'deb https://apt.llvm.org/jammy/ llvm-toolchain-jammy-20 main' | tee -a /etc/apt/sources.list.d/llvm.list \
  && apt-get update \
  && apt-get install -y --no-install-recommends clang-20 clang-format-20 clang-tidy-20
ENV PATH="/usr/lib/llvm-20/bin:${PATH}"
ENV CC=clang-20
ENV CXX=clang++-20
