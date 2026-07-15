# dd-octo-sts is required in release jobs, where we need to obtain a short-lived GitHub
# token to push changes, open PRs, create GH releases, etc. To upgrade:
# - `crane ls registry.ddbuild.io/dd-octo-sts | sort -rV | head` to find latest version
# - `crane digest registry.ddbuild.io/dd-octo-sts:<version-tag>` to get sha256 digest
ARG DD_OCTO_STS_TAG=v1.10.6
ARG DD_OCTO_STS_DIGEST=sha256:2b1a69019340da2be0e1e71de4d827acee2fdc5ee38f676dd96aa88c112ed6e8

# commit-headless is required in the prepare-release job, in order to sign commits that
# are pushed to origin. To upgrade:
# - `crane ls registry.ddbuild.io/commit-headless | sort -rV | head`
# - `crane digest registry.ddbuild.io/commit-headless:<version-tag>`
ARG COMMIT_HEADLESS_TAG=v3.3.0
ARG COMMIT_HEADLESS_DIGEST=sha256:754a5bc4f2451bbd355ab84bce0d57952c143308d219a17306ba0947852f6711

# Alias images that contain binaries for internal Datadog tools, so we can pull those
# binaries into our image at a later stage
FROM registry.ddbuild.io/dd-octo-sts:${DD_OCTO_STS_TAG}@${DD_OCTO_STS_DIGEST} AS dd_octo_sts
FROM registry.ddbuild.io/commit-headless:${COMMIT_HEADLESS_TAG}@${COMMIT_HEADLESS_DIGEST} AS commit_headless

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
# declared in pyproject.toml
RUN add-apt-repository ppa:deadsnakes/ppa \
  && apt update \
  && apt install --no-install-recommends -y python3.12 python3.12-venv \
  && python3.12 -m ensurepip

# We still need the stock Python 3.10 to remain installed as the system-wide default
# (for add-apt-repository etc.), so use an env var to override Python interpreter
# resolution in our CMake build
ENV DD_SYSTEM_PYTHON=/usr/bin/python3.12

# Use Ninja by default for CMake builds on Linux: it's ~6x faster than Make
ENV CMAKE_GENERATOR=Ninja

# Install the binaries for:
# - dd-octo-sts, which allows our CI release jobs to exchange GitLab OIDC tokens for
#   short-lived GitHub tokens, according to the trust policies described in
#   .github/chainguard/
# - commit-headless, which allows our CI release jobs to sign their commits
COPY --from=dd_octo_sts /usr/local/bin/dd-octo-sts /usr/local/bin/dd-octo-sts
COPY --from=commit_headless /commit-headless /usr/local/bin/commit-headless

# Install the authanywhere binary: this will let our CI runner use its GitLab OIDC/CI
# identity to obtain a Datadog internal service-auth JWT (with aud: rapid-ai-platform)
# so it can make requests to the AI Gateway in scripts that make LLM calls
RUN if [ $(uname -m) = x86_64 ]; then AAA="amd64"; else AAA="arm64"; fi; \
  wget "binaries.ddbuild.io/dd-source/authanywhere/LATEST/authanywhere-linux-${AAA}" \
  && mv "authanywhere-linux-${AAA}" /bin/authanywhere \
  && chmod +x /bin/authanywhere

# Install the GitHub CLI for use in release jobs
RUN wget -qO /usr/share/keyrings/githubcli-archive-keyring.gpg https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
  | tee /etc/apt/sources.list.d/github-cli.list >/dev/null \
  && apt-get update \
  && apt-get install -y --no-install-recommends gh

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

# Build `ci/dd-sdk-cpp/clang20-x86` with: --target clang20-x86
# Extends clang20 with 32-bit cross-compilation support: gcc-multilib provides the
# 32-bit glibc/libgcc stubs that Clang needs for -m32, and the :i386 library packages
# provide 32-bit headers and link libraries for our build dependencies.
FROM clang20 AS clang20-x86
RUN dpkg --add-architecture i386 \
  && apt-get update \
  && apt-get install -y --no-install-recommends \
    -o Dpkg::Options::="--force-overwrite" \
    g++-multilib \
    libcurl4-openssl-dev:i386 \
    libssl-dev:i386 \
    zlib1g-dev:i386 \
    uuid-dev:i386
# -m32:                  compile and link as 32-bit x86
# -D_FILE_OFFSET_BITS=64: redirect stat()/fopen()/etc. to their 64-bit equivalents
#                         so that 32-bit code handles 64-bit inodes correctly on
#                         Linux hosts with overlayfs (e.g. Kubernetes CI runners),
#                         where inode numbers exceed 2^32 and 32-bit stat() returns
#                         EOVERFLOW without this flag
ENV CFLAGS="-m32 -D_FILE_OFFSET_BITS=64"
ENV CXXFLAGS="-m32 -D_FILE_OFFSET_BITS=64"
ENV LDFLAGS=-m32
