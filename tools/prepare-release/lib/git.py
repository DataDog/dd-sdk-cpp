# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for running git operations using the git CLI.
"""
import json
import subprocess
from datetime import datetime, timedelta, timezone

from lib.github import gh


def git(*args: str) -> str:
    """Run a git command and return its stdout, raising on failure."""
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout.rstrip()


def merged_pr_numbers(base: str) -> list[int]:
    """
    Return the sorted numbers of the PRs merged since `base`: those whose merge commit,
    as GitHub records it, is reachable from HEAD but not from `base`.
    """
    # Commit subjects can't identify every PR: when a stack of PRs is merged from the
    # top, only the top PR gets a merge commit. GitHub marks each PR in the stack as
    # merged, recording the top PR's merge commit (or the squash or rebase commit, for
    # other merge methods) as that PR's merge commit.

    # Map the SHA of each commit in the range to its committer date, as a Unix timestamp
    commit_time_by_sha: dict[str, int] = {}
    for line in git("log", "--format=%H %ct", f"{base}..HEAD").splitlines():
        sha, timestamp = line.split()
        commit_time_by_sha[sha] = int(timestamp)
    if not commit_time_by_sha:
        return []

    # GitHub can't filter PRs by revision range, so search for PRs merged since the
    # oldest commit in the range, then match their merge commits against the range.
    # GitHub creates each PR's merge commit when it merges the PR, so that commit's date
    # bounds the PR's merge date. Allow a day before it, in case of clock skew.
    oldest = datetime.fromtimestamp(min(commit_time_by_sha.values()), tz=timezone.utc)
    since = (oldest - timedelta(days=1)).date().isoformat()
    limit = 1000
    prs = json.loads(gh(
        "pr", "list", "--state", "merged", "--search", f"merged:>={since}",
        "--limit", str(limit), "--json", "number,mergeCommit",
    ))
    # GitHub's search returns at most 1000 results, so a full page may be missing PRs
    if len(prs) >= limit:
        raise RuntimeError(f"At least {limit} PRs were merged since {since}; cannot be sure all were listed")

    numbers: list[int] = []
    for pr in prs:
        # gh reports each commit's SHA as its 'oid' (Git object ID)
        merge_commit = pr["mergeCommit"]
        merge_commit_is_in_release = merge_commit is not None and merge_commit["oid"] in commit_time_by_sha
        if merge_commit_is_in_release:
            numbers.append(pr["number"])
    return sorted(numbers)
