# This Dockerfile describes the build environment used for Linux builds of dd-sdk-cpp,
# with various toolchain versions defined using multi-stage targets
FROM ubuntu:22.04 AS base

# Suppress prompts; default to UTC
ENV DEBIAN_FRONTEND=noninteractive
RUN ln -sf /usr/share/zoneinfo/Etc/UTC /etc/localtime

# Install required tools and libraries
# - ca-certificates, gpg, gpg-agent, wget: For third-party apt repositories
# - libcurl4-openssl-dev, libssl-dev:      For default libcurl HTTP client impl
# - uuid-dev:                              uuid/uuid.h, for datadog::UUID on Linux
# - git && python3.10-venv:                For tools/validate-event-types
# - ninja-build:                           Build system for use as CMake generator
RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  gpg \
  gpg-agent \
  wget \
  libcurl4-openssl-dev \
  libssl-dev \
  uuid-dev \
  git \
  python3.10-venv \
  ninja-build

# Use Ninja by default for CMake builds on Linux: it's ~6x faster than Make
ENV CMAKE_GENERATOR=Ninja

# Install CMake from Kitware repo to get the latest version
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy-rc main' | tee -a /etc/apt/sources.list.d/kitware.list >/dev/null \
  && apt-get update \
  && apt-get install -y --no-install-recommends cmake

# Install the Vault CLI: while it's not required to build the library, we need it to
# access CI secrets in certain jobs (e.g. integration-test-linux)
RUN apt-get install -y libcap2-bin \
  && wget -O - https://apt.releases.hashicorp.com/gpg | gpg --dearmor -o /usr/share/keyrings/hashicorp-archive-keyring.gpg \
  && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/hashicorp-archive-keyring.gpg] https://apt.releases.hashicorp.com jammy main" | tee /etc/apt/sources.list.d/hashicorp.list > /dev/null \
  && apt-get update \
  && apt-get install -y --no-install-recommends vault

# Define the various compiler toolchains we want to build with using Docker

# Build `ci/dd-sdk-cpp/gcc11` with: --target gcc11
FROM base AS gcc11
RUN apt-get update && apt-get install -y --no-install-recommends software-properties-common \
  && add-apt-repository ppa:ubuntu-toolchain-r/test \
  && apt-get update \
  && apt-get install -y --no-install-recommends gcc-11 g++-11
ENV CC=gcc-11
ENV CXX=g++-11

# Build `ci/dd-sdk-cpp/gcc13` with: --target gcc13
FROM base AS gcc13
RUN apt-get update && apt-get install -y --no-install-recommends software-properties-common \
  && add-apt-repository ppa:ubuntu-toolchain-r/test \
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
