#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import re
import os
import sys
import argparse
import subprocess
from pathlib import Path


def git(*args: str) -> str:
    """Run a git command and return its stdout, raising on failure."""
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout.strip()


def gh(*args: str) -> str:
    """Run a gh command and return its stdout, raising on failure."""
    env = {**os.environ, "GH_PAGER": ""}
    result = subprocess.run(["gh", *args], capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(f"gh {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def read_project_version() -> str:
    """Reads current SDK version from project() directive in root CMakeLists.txt."""
    text = Path("CMakeLists.txt").read_text()
    m = re.search(r'project\s*\([^)]*\bVERSION\s+(\S+)', text)
    if not m:
        raise ValueError("Could not find VERSION in CMakeLists.txt project() directive")
    return m.group(1)


def read_changelog_section(version: str) -> str:
    """Extracts the contents of CHANGELOG.md for the given version."""
    text = Path("CHANGELOG.md").read_text()
    m = re.search(r'^## ' + re.escape(version) + r'\s*$', text, re.MULTILINE)
    if not m:
        raise RuntimeError(f"No CHANGELOG.md section found for {version}")
    rest = text[m.end():]
    next_h2 = re.search(r'^## ', rest, re.MULTILINE)
    return rest[:next_h2.start()].strip() if next_h2 else rest.strip()


def main():
    parser = argparse.ArgumentParser(description=(
        "Checks project version in CMakeLists.txt: if it's a fresh version bump, tags "
        "the latest revision in origin/main with that version and creates a GitHub "
        "release with build artifacts collected from earlier pipeline jobs"
    ))
    parser.add_argument("archives", nargs="+")
    args = parser.parse_args()

    # Read the canonical SDK version number from CMakeLists.txt
    sdk_version = read_project_version()

    # Check to see if there's already a tag with this version number: if so, we've
    # previously completed the release process and the version number hasn't been bumped
    # since then, so we'll exit early without creating a release
    git("fetch", "--tags")
    if git("tag", "-l", sdk_version) == sdk_version:
        # Exit code 2 is listed under allow_failure in the CI job, so that the result
        # displayed in the GitLab UI is unambiguous:
        # - Green (Pass): New release was published.
        # - Red (Fail): Attempt to publish new release failed.
        # - Orange (Warning): No release was attempted.
        print(f"Tag {sdk_version} already exists; no release needed.")
        sys.exit(2)

    # We should be passed a list of paths to archive files containing prebuilt binaries
    # to be included in this release as artifacts: perform some basic sanity checks to
    # verify that we have at least one archive for each supported platform at the
    # expected version number
    linux_prefix = f"ddsdkcpp-v{sdk_version}-linux-"
    macos_prefix = f"ddsdkcpp-v{sdk_version}-macos-"
    windows_prefix = f"ddsdkcpp-v{sdk_version}-windows-"
    missing_platforms: set[str] = {'linux', 'macos', 'windows'}
    print(f"Publishing release with {len(args.archives)} artifacts:")
    for archive_relpath in args.archives:
        print(f"- {archive_relpath}")
        filename: str = os.path.basename(archive_relpath)
        if filename.startswith(linux_prefix):
            missing_platforms.discard('linux')
        elif filename.startswith(macos_prefix):
            missing_platforms.discard('macos')
        elif filename.startswith(windows_prefix):
            missing_platforms.discard('windows')
    if missing_platforms:
        raise RuntimeError(f"Missing archives for {', '.join(missing_platforms)}")
    
    # Pull the release notes for this version from the relevant h2-delimited section of
    # CHANGELOG.md
    release_notes = read_changelog_section(sdk_version)

    # Identify the PR that merged this release's changes into `main`, so we can include
    # a link to it in our final `main` -> `develop` PR
    release_pr_number_str = gh(
        "pr", "list",
        "--base", "main",
        "--head", f"release/{sdk_version}",
        "--state", "merged",
        "--json", "number",
        "--jq", ".[0].number"
    ).strip()
    if not release_pr_number_str or release_pr_number_str == "null":
        raise RuntimeError(f"Could not find a merged PR from release/{sdk_version} into main")
    release_pr_number = int(release_pr_number_str)

    # Use the gh CLI (authenticated via GITHUB_TOKEN) to create a new release at the
    # commit from which these artifacts where built: this implicitly creates a tag at
    # the same revision, and it uploads all provided files as artifacts
    head_sha = git("rev-parse", "HEAD")
    gh(
        "release", "create", sdk_version,
        "--target", head_sha,
        "--title", sdk_version,
        "--notes", release_notes,
        *args.archives
    )

    # Now that the release is complete, we want to merge from `main` to `develop`,
    # ensuring that modifications made to CMakeLists.txt, CHANGELOG.md, etc. in the
    # release branch make their way back to develop. If such a PR already exists, we can
    # finish here.
    existing_pr_url = gh(
        "pr", "list",
        "--base", "develop",
        "--head", "main",
        "--state", "open",
        "--json", "url",
        "--jq", ".[0].url"
    ).strip()
    if existing_pr_url and existing_pr_url != "null":
        print(f"Release {sdk_version} complete.")
        print(f"A merge-down PR already exists: {existing_pr_url}")
        return

    # Otherwise, no merge-down PR is open, so we can create one
    pr_body = '\n'.join([
        f"The release of **dd-sdk-cpp** version **{sdk_version}** has been merged to "
        "`main`, and a new GitHub release has been successfully published from that "
        "revision.",
        "",
        f"- **Release PR:** #{release_pr_number}",
        f"- **GitHub Release:** [{sdk_version}](https://github.com/DataDog/dd-sdk-cpp/releases/tag/{sdk_version})",
        f"- **Commit SHA:** `{head_sha}` (tagged `{sdk_version}`)",
        "",
        "Merging this PR to `develop` will persist the bookkeeping changes _(version "
        "bumps, changelog updates, etc.)_ from the release branch.",
        "",
    ])

    # At this point, we've successfully created a release: regardless of whether we
    # successfully create the final merge-down PR, the release was published and we
    # should signal that fact clearly with exit code 0 (Green job == release published)
    try:
        pr_url = gh(
            "pr", "create",
            "--base", "develop",
            "--head", "main",
            "--title", f"chore: Merge changes from release/{sdk_version} to develop",
            "--body", pr_body
        ).strip()
    except RuntimeError as e:
        print(f"WARNING: Release {sdk_version} complete, but failed to open merge-down PR: {e}")
        return

    print(f"Release {sdk_version} complete.")
    print(f"Final merge-down PR ready for review: {pr_url}")


if __name__ == '__main__':
    main()
