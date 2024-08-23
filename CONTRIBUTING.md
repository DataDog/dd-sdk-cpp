
# Contributing

First of all, thanks for contributing!

This document provides some basic guidelines for contributing to this repository. To propose improvements, feel free to submit a PR or open an Issue.

Note: Datadog requires that all commits within this repository must be signed, including those within external contribution PRs. Please ensure you have followed GitHub's Signing Commits guide before proposing a contribution. PRs lacking signed commits will not be processed and may be rejected.

## Found a bug?

For any urgent matters (such as outages) or issues concerning the Datadog service or UI, contact our support team via https://docs.datadoghq.com/help/ for direct, faster assistance.

You may submit a bug report concerning the Datadog Plugin for Flutter by opening a GitHub Issue. Use appropriate template and provide all listed details to help us resolve the issue.

## Getting Started

### Install clang-tidy and clang-format

The SDK uses `clang-tidy` to enforce some best practices. If you are working on macOS, you will need to install `clang-tidy` as it is not part of the installation of `clang` included with macOS.

The easiest way to install `clang-tidy` is to use `brew` to install all of `llvm`, then symlink the necessary tools onto your `PATH`.

```bash
brew install llvm
ln -s "$(brew --prefix llvm)/bin/clang-format" "/usr/local/bin/clang-format"
ln -s "$(brew --prefix llvm)/bin/clang-tidy" "/usr/local/bin/clang-tidy"
ln -s "$(brew --prefix llvm)/bin/clang-apply-replacements" "/usr/local/bin/clang-apply-replacements"
```
