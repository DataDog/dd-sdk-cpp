
# Contributing

First of all, thanks for contributing!

This document provides some basic guidelines for contributing to this repository. To propose improvements, feel free to submit a PR or open an Issue.

Note: Datadog requires that all commits within this repository must be signed, including those within external contribution PRs. Please ensure you have followed GitHub's Signing Commits guide before proposing a contribution. PRs lacking signed commits will not be processed and may be rejected.

## Found a bug?

For any urgent matters (such as outages) or issues concerning the Datadog service or UI, contact our support team via https://docs.datadoghq.com/help/ for direct, faster assistance.

You may submit a bug report concerning the Datadog Plugin for Flutter by opening a GitHub Issue. Use appropriate template and provide all listed details to help us resolve the issue.

## Getting Started

### Install clang-tidy and clang-format

The SDK uses `clang-tidy` and `clang-format`  to enforce some best practices. If you are working on macOS, you will need to install `clang-tidy` and ensure you are using `clang-format-15` as it is not part of the installation of `clang` included with macOS.

The easiest way to do this is to install `clang-15` from MacPorts, then add it to your path.

```bash
sudo port install clang-15
export PATH=/opt/local/libexec/llvm-15/bin:$PATH
```
