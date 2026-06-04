#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
from __future__ import annotations

import re
import sys
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from lib.git import git, merged_pr_numbers
from lib.github import gh, fetch_pr, PR, CommitKind
from lib.changelog import (
    run_grouped_prs_prompt,
    run_changelog_entry_list_prompt,
    run_cleanup_prompt,
    PRGroup,
    ChangelogEntryList
)
from lib.llm.ai_gateway import make_ai_gateway_client
from lib.llm.costs import print_llm_costs


def find_prs_since_last_release(last_release_tag: str) -> list[PR]:
    # Use the git CLI to retrieve the set of PRs merged since the last release
    numbers = merged_pr_numbers(last_release_tag)
    print(f"Found {len(numbers)} merged PRs", file=sys.stderr)

    # Use the gh CLI to fetch titles and descriptions for all those PRs
    print("Fetching PR details...", file=sys.stderr)
    with ThreadPoolExecutor(max_workers=8) as executor:
        prs = list(executor.map(fetch_pr, numbers))
    
    return prs


def read_project_version() -> str:
    """Reads current SDK version from project() directive in root CMakeLists.txt."""
    text = Path("CMakeLists.txt").read_text()
    m = re.search(r'project\s*\([^)]*\bVERSION\s+(\S+)', text)
    if not m:
        raise ValueError("Could not find VERSION in CMakeLists.txt project() directive")
    return m.group(1)


def write_project_version(new_version: str) -> None:
    """Updates CMakeLists.txt so that its project() directive reflects the new version."""
    p = Path("CMakeLists.txt")
    text = p.read_text()
    new_text, n = re.subn(r'(project\s*\([^)]*\bVERSION\s+)\S+', rf'\g<1>{new_version}', text)
    if n == 0:
        raise ValueError("Could not find VERSION in CMakeLists.txt project() directive")
    p.write_text(new_text)


def read_changelog() -> str:
    """Returns the full contents of CHANGELOG.md, returning an empty string if not found."""
    p = Path("CHANGELOG.md")
    return p.read_text() if p.exists() else ""


def parse_semver(s: str) -> tuple[int, int, int]:
    match = re.compile(r'(\d+)\.(\d+)\.(\d+)').match(s)
    if not match:
        raise ValueError(f'Not a semantic version string: {s}')
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def main() -> None:
    parser = argparse.ArgumentParser(description=(
        "Cut a new release/X.Y.Z branch from develop, update CMakeLists.txt with new "
        "version, LLM-generate additions to CHANGELOG.md based on PRs, then open a PR "
        "to merge release/X.Y.Z into main"
    ))
    parser.add_argument(
        "--bump", choices=["patch", "minor", "major"], help=(
            "Force this kind of version bump; don't infer from conventional-commit "
            "prefixes on PR titles"
        )
    )
    args = parser.parse_args()

    ## == Phase 1: Sanity-check config, find PRs being released, and bump SDK version

    # The source of truth for the SDK version is `project(... VERSION X.Y.Z ...)` in the
    # root CMakeLists.txt file: read it to determine the current version
    old_sdk_version = read_project_version()
    old_sdk_version_sem = parse_semver(old_sdk_version)

    # We create an 'X.Y.Z' tag (no 'v' prefix) for each release: verify that we already
    # have such a tag for the current (pre-bump) version, so that we can use it as the
    # base revision for determining which changes are included in this new release. Note
    # that this script runs in CI, which may have performed a shallow clone, so we need
    # to fetch tags first.
    git("fetch", "--tags")
    if git("tag", "-l", old_sdk_version) != old_sdk_version:
        raise RuntimeError(
            f"No tag exists for version {old_sdk_version}! Unable to determine base "
            f"revision for changes included in the next release. Please re-run this "
            f"job after the release process for {old_sdk_version} is complete."
        )
    
    # Read the contents of CHANGELOG.md: this process assumes at least one release has
    # already been made (the very first release should populate CHANGELOG.md and push a
    # tag manually), so we expect the file to exist and be non-empty
    old_changelog_text = read_changelog()
    if not old_changelog_text:
        raise RuntimeError("CHANGELOG.md does not exist or is empty")

    # CHANGELOG.md format is: an H2 section (e.g. '## 0.2.0') for each release, in
    # descending order starting from the most recent, with no preceding text or headings
    version_heading_regex = re.compile(r'^##\s(.*)$', re.MULTILINE)

    # Find the first H2 that appears in the changelog: it must exactly match the
    # pre-bump version. We only want to auto-update CHANGELOG.md if it actually reflects
    # the expected state; any errors here indicate that someone needs to manually fix
    # the changelog to ensure that we're recording accurate history and not skipping or
    # clobbering release notes.
    most_recent_version_match = next(version_heading_regex.finditer(old_changelog_text), None)
    if not most_recent_version_match:
        raise RuntimeError("CHANGELOG.md does not contain an H2 (e.g. '## 0.2.0')")
    if most_recent_version_match.start() != 0:
        raise RuntimeError(f"CHANGELOG.md has superfluous text before first H2 ({most_recent_version_match.group()})")
    if most_recent_version_match.group(1) != old_sdk_version:
        raise RuntimeError(f"Latest release in CHANGELOG.md is {most_recent_version_match.group(1)}; expected {old_sdk_version}")

    # Get a list of PRs whose changes are included in this release: this finds every
    # GitHub PR that has been merged into the current branch (i.e. `develop`) since the
    # last revision
    prs = find_prs_since_last_release(old_sdk_version)

    # Use PR titles to determine whether to bump the major, minor, or patch version,
    # unless explicitly overridden by setting `BUMP` as a GitLab CI job variable
    bump = args.bump
    if not bump:
        # Note that we use PR titles, not commits, so the expected convention is:
        # - PR title must begin with `feat:` if it should bump the minor version,
        # - PR title must begin with `feat!:`|`fix!:` or include `BREAKING` in the title
        #   if it should bump the major version
        # - If neither of the above apply, a PR is assumed to affect the patch version
        if any(pr.is_breaking for pr in prs):
            bump = 'major'
        elif any(pr.kind == CommitKind.FEAT for pr in prs):
            bump = 'minor'
        else:
            bump = 'patch'

    # Perform the version bump to get our new SDK version
    if bump == 'major':
        major, _, _ = old_sdk_version_sem
        new_sdk_version_sem = major + 1, 0, 0
    elif bump == 'minor':
        major, minor, _ = old_sdk_version_sem
        new_sdk_version_sem = major, minor + 1, 0
    else:
        assert bump == 'patch'
        major, minor, patch = old_sdk_version_sem
        new_sdk_version_sem = major, minor, patch + 1
    new_sdk_version = '%d.%d.%d' % new_sdk_version_sem

    # Write the new version into CMakeLists.txt: once this change hits `main`, the CI
    # pipeline will build release artifacts stamped with this version, and the
    # 'publish-release' CI job will use this new version to name the release
    write_project_version(new_sdk_version)
    print(f"Updated project version in CMakeLists.txt: {old_sdk_version} -> {new_sdk_version}")

    ## == Phase 2: Feed PRs into a multi-stage LLM pipeline to update CHANGELOG.md

    # Establish the subset of PRs that we'll feed into our LLM passes in order to
    # generate new entries for CHANGELOG.md in this release: we keep 'chore:' PRs out of
    # the context entirely, since we presume that they have no user-facing impact, but
    # something like 'chore!: BREAKING API VERSION BUMP' would still be included
    significant_prs = [pr for pr in prs if pr.is_breaking or pr.kind != CommitKind.CHORE]
    if significant_prs:
        # Run an initial LLM pass, providing only the list of PR numbers and titles, to
        # group related PRs by subject area
        client = make_ai_gateway_client()
        groups = run_grouped_prs_prompt(client, significant_prs).groups

        # Print the results of that grouping pass
        for group in groups:
            group_pr_numbers = {pr.number for pr in group.prs}
            numbers_str = ','.join(str(n) for n in sorted(group_pr_numbers))
            print(f'{group.label} ({numbers_str})')

        # For each of those groups, provide the full descriptions of all relevant PRs to
        # another LLM pass, prompting it to synthesize 0 or more changelog entries based on
        # the changes described in those PRs, and collecting the full set of results from
        # all groups
        changelog = ChangelogEntryList()
        for group in groups:
            group_pr_numbers = {pr.number for pr in group.prs}
            group_prs = [pr for pr in significant_prs if pr.number in group_pr_numbers]
            group_res = run_changelog_entry_list_prompt(client, group.label, group_prs)
            changelog.breaking_changes += group_res.breaking_changes
            changelog.features += group_res.features
            changelog.fixes += group_res.fixes

        # Run a final LLM pass to deduplicate and reorganize the changelog items
        changelog = run_cleanup_prompt(client, changelog)
    else:
        # If this release includes no PRs with significant user-facing impact, skip the
        # LLM passes and just write an empty changelog
        groups: list[PRGroup] = []
        changelog = ChangelogEntryList()

    # Prepend the changes to a new section in CHANGELOG.md (we already determined
    # up-front that the topmost section in the existing changelog is for the prior
    # revision)
    with open('CHANGELOG.md', 'w') as fp:
        # Write an H2 for our new version
        fp.write(f'## {new_sdk_version}\n\n')

        # If we had no significant PRs, or if the LLM passes decided that none of the
        # PRs being released had any user-facing impact, write a placeholder message.
        if len(changelog.breaking_changes) + len(changelog.features) + len(changelog.fixes) == 0:
            # This is a likely signal that a human needs to review CHANGELOG.md and
            # clarify the acutal intent of the release: this will be called out in the
            # PR body with a note reading "No recorded changes 🤔"
            fp.write("- Maintenance release; no significant changes.\n\n")

        # Otherwise, write the final list of entries for each section, under an H3,
        # omitting sections that have no entries
        for section, entries in [
            ("### Breaking Changes", changelog.breaking_changes),
            ("### Features", changelog.features),
            ("### Fixes", changelog.fixes),
        ]:
            if entries:
                fp.write(f'{section}\n\n')
                for entry in entries:
                    fp.write(f'- {entry.text}\n')
                fp.write('\n')
        
        # Append the original text of CHANGELOG.md after our new contents
        fp.write(old_changelog_text)

    print('Wrote CHANGELOG.md.')

    ## == Phase 3: Format PR description

    # Get the commit hash of the revision we're cutting this release from (typically the
    # tip of `develop`), prior to making any local commits, so we can provide it to
    # commit-headless as --head-sha
    base_head_sha = git("rev-parse", "HEAD")

    # A typical prepare-release job will be run from the lastest commit in `develop`, so
    # verify that we're actually cutting the release from such a state. It's possible
    # that this won't be the case if we're testing or deliberately releasing from an
    # older state, so flag it as a warning in the PR description but don't fail outright
    base_branch = git("branch", "--show-current")
    git("fetch", "origin", base_branch)
    remote_head_sha = git("rev-parse", f"origin/{base_branch}")

    # Establish the name of the release branch we'll be creating
    branch_name = f"release/{new_sdk_version}"

    # Title used for PR and commit
    pr_title = f"chore: Prepare release {new_sdk_version}"

    # Details listed in the PR body for sanity-checking, e.g.
    # - SDK version: 1.2.3 -> 1.2.4
    # - Base branch: develop
    # - Base revision: fffffffff (matches `origin/develop` at time of PR creation)
    sdk_version_li = f"- **SDK version:** `{old_sdk_version}` &rarr; `{new_sdk_version}`"
    if bump == 'major':
        sdk_version_li += " ***(‼️ MAJOR VERSION BUMP ‼️)***"

    base_branch_li = f"- **Base branch:** `{base_branch}`"
    if base_branch != "develop":
        base_branch_li += " ***(⚠️ RELEASE IS NOT CUT FROM `develop` ⚠️)***"

    base_revision_li = f"- **Base revision:** `{base_head_sha}`"
    if base_head_sha != remote_head_sha:
        base_revision_li += f" ***(⚠️ OUTDATED; LATEST COMMIT IN `origin/{base_branch}` IS `{remote_head_sha}`)***"
    else:
        base_revision_li += f" _(matches `origin/{base_branch}` at time of PR creation)_"

    # Summarized details of auto-generate CHANGELOG edits, to be listed under
    # "[changelog] consists of:", e.g.
    # - 3 feature changes
    # - 1 fix
    changelog_summary_lis: list[str] = []
    if changelog.breaking_changes:
        li = f"- {len(changelog.breaking_changes)} breaking change"
        if len(changelog.breaking_changes) > 1:
            li += "s"
        changelog_summary_lis.append(li)
    if changelog.features:
        li = f"- {len(changelog.features)} feature change"
        if len(changelog.features) > 1:
            li += "s"
        changelog_summary_lis.append(li)
    if changelog.fixes:
        li = f"- {len(changelog.fixes)} fix"
        if len(changelog.fixes) > 1:
            li += "es"
        changelog_summary_lis.append(li)
    if not changelog_summary_lis:
        changelog_summary_lis.append("- No recorded changes 🤔")

    # Construct the main body of the PR, summarizing the details of the release and the
    # auto-drafted CHANGELOG contents
    pr_body_lines = [
        "This PR prepares a new release for merge into `main`:",
        "",
        sdk_version_li,
        base_branch_li,
        base_revision_li,
        "",
        "Approving and merging this PR to `main` will trigger a `publish-release` CI "
        "job, which will build release artifacts and create a new GitHub release.",
        "",
        "### Changes in this release",
        "",
        f"See [CHANGELOG.md](https://github.com/DataDog/dd-sdk-cpp/blob/{branch_name}/CHANGELOG.md) "
        "for a full description of user-facing changes made in this release. The "
        "initial draft was generated from the PRs listed below, and it consists of:",
        "",
    ] + changelog_summary_lis + [
        "",
        "You may edit `CHANGELOG.md` directly in the **Files changed** tab, or by "
        "pushing new commits to this branch.",
        "",
    ]

    # Finish the PR body by listing all the PRs we identified as belonging to this
    # release
    pr_numbers_noted: set[int] = set()
    for group in groups:
        pr_body_lines += [
            f"#### {group.label}",
            "",
        ]
        for pr in group.prs:
            pr_body_lines += [f"- #{pr.number}"]
            pr_numbers_noted.add(pr.number)
        pr_body_lines += [""]
    
    # Include an additional section to note any PRs that we didn't categorize in the
    # CHANGELOG process because they were labeled 'chore:'
    other_pr_numbers = [pr.number for pr in prs if pr.number not in pr_numbers_noted]
    if other_pr_numbers:
        pr_body_lines += [
            "#### Other PRs not considered relevant for CHANGELOG",
            "",
        ]
        for pr_number in other_pr_numbers:
            pr_body_lines += [f"- #{pr_number}"]
        pr_body_lines += [""]

    # We've finished formatting our PR description
    pr_body = "\n".join(pr_body_lines)

    # Print the details of the PR that we'll create if the remaining steps succeed
    print("=" * 80)
    print(pr_title)
    print('-' * 80)
    print(pr_body)
    print("=" * 80)

    ## == Phase 4: Commit to release/X.Y.Z, push, and open a PR into main

    # At this point, we should have made changes to CMakeLists.txt and CHANGELOG.md: if
    # either file is unchanged, or if any other files have untracked changes, we're not
    # in the expected state and we should abort the release
    status = git("status", "--porcelain")
    changed_files = {line[3:] for line in status.splitlines() if line.strip()}
    if changed_files != {"CMakeLists.txt", "CHANGELOG.md"}:
        raise RuntimeError(f"Unexpected working tree state prior to commit:\n{status}")
    
    # Create a release/X.Y.Z branch, add all pending changes to it, and commit
    git("checkout", "-b", branch_name)
    git("add", ".")
    git("commit", "-m", pr_title)

    # Use `commit-headless push` to sign our commit and push it to origin, ensuring that
    # we'll be able to merge it into `main`
    subprocess.check_call([
        "commit-headless", "push",
        "--target", "DataDog/dd-sdk-cpp",
        "--branch", branch_name,
        "--create-branch",
        "--head-sha", base_head_sha
    ])

    # Use `gh` to open a PR that will merge all the changes from this release into
    # `main`.
    pr_url = gh(
        "pr", "create",
        "--title", pr_title,
        "--base", "main",
        "--head", branch_name,
        "--body", pr_body
    ).strip()

    # At this point, the release is ready for human review: we can edit the CHANGELOG,
    # cherry-pick additional changes, drop the branch and re-run later if we decide we
    # want to wait for more changes, etc. - once a human merges our release branch to
    # `main`, the CI pipeline will pick up on the updated version number in
    # CMakeLists.txt and run the publish-release job, which creates a GitHub release
    # with prebuilt binaries, pushes a new tag, and merges `main` -> `develop`.

    # Summarize LLM token usage and estimated cost (our `run_*_prompt()` calls
    # automatically recorded their usage stats globally via `record_llm_call_costs()`)
    print_llm_costs()

    # Print success message, confirm version bump, and link to PR
    print(f'Release prepared successfully: {old_sdk_version} -> {new_sdk_version}')
    print(f'Ready for review: {pr_url}')


if __name__ == "__main__":
    main()
