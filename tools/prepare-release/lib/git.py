# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for running git operations using the git CLI.
"""
import re
import subprocess


def git(*args: str) -> str:
    """Run a git command and return its stdout, raising on failure."""
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout.strip()


def merged_pr_numbers(base: str) -> list[int]:
    """Return the sorted, unique set of PR numbers merged since `base`."""
    # When merging PRs we tend to always use merge commits, but we'll also handle
    # squash-and-merge commits to be safe
    merge_commit_pattern = re.compile(r"Merge pull request #(\d+)")
    squash_merge_pattern = re.compile(r"\(#(\d+)\)$")

    # Get all commits; don't use --merges so we see squashes too
    log = git("log", f"{base}..HEAD", "--format=%s")
    numbers: set[int] = set()
    for line in log.splitlines():
        match = merge_commit_pattern.match(line) or squash_merge_pattern.search(line)
        if match:
            numbers.add(int(match.group(1)))
    return sorted(numbers)
