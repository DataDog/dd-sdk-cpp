# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for managing local, read-only copies of public GitHub repositories.
"""
import os

from git import Repo

__github_url_pattern__ = 'https://github.com/DataDog/%s.git'
__path_pattern__ = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '%s'))


def _is_commit_sha(s: str) -> bool:
    return bool(s) and all(c in '0123456789abcdef' for c in s)


def _resolve_commit_ref(commit_ref: str) -> str:
    # Treat pure hex strings as a direct commit SHA
    if _is_commit_sha(commit_ref):
        return commit_ref
    
    # Assume everything else is a branch, and assume we want the latest remote changes
    if commit_ref.startswith('origin/'):
        return commit_ref
    return f'origin/{commit_ref}'


def _hard_reset_to(repo: Repo, commit_ref: str):
    # Fetch latest remote repository state
    print(f'> git fetch')
    repo.remotes.origin.fetch()

    # Hard reset to the desired branch/commit
    print(f'> git reset --hard {commit_ref}')
    repo.git.reset('--hard', commit_ref)

    # Remove all untracked files or local changes
    print(f'> git clean -fdx')
    repo.git.clean('-fdx')


def fetch_repo(name: str, commit_ref: str) -> Repo:
    """
    Clones a public DataDog GitHub repo locally, within tools/validate-event-types, and
    ensures that it's in a clean state at the given revision. If a local repository
    already exists, any local modifications or untracked files will be discarded without
    warning.
    """
    commit_ref = _resolve_commit_ref(commit_ref)

    local_path = __path_pattern__ % name
    if os.path.isdir(os.path.join(local_path, '.git')):
        repo = Repo(local_path)
        _hard_reset_to(repo, commit_ref)
        return repo

    github_url = __github_url_pattern__ % name
    print(f'> git clone {github_url}')
    repo = Repo.clone_from(github_url, local_path)
    print(f'> git checkout {commit_ref}')
    repo.git.checkout(commit_ref)
    return repo
