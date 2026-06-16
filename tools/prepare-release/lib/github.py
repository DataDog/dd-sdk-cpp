# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for making GitHub API calls via the gh CLI.
"""
import os
import re
import json
import subprocess
from enum import Enum
from dataclasses import dataclass


class CommitKind(Enum):
    FEAT = "feat"
    FIX = "fix"
    CHORE = "chore"


@dataclass
class PR:
    number: int
    title: str
    body: str
    kind: CommitKind
    is_breaking: bool


def gh(*args: str) -> str:
    """Run a gh command and return its stdout, raising on failure."""
    env = {**os.environ, "GH_PAGER": ""}
    result = subprocess.run(["gh", *args], capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(f"gh {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def parse_title(title: str) -> tuple[CommitKind, bool]:
    """Parse the conventional commit prefix and breaking-change flag from a PR title."""
    m = re.compile(r"^(feat|fix|chore)(!)?:").match(title)
    kind = CommitKind(m.group(1)) if m else CommitKind.FIX  # Assume 'fix:' if no prefix
    is_breaking = bool(m and m.group(2)) or "BREAKING" in title
    return kind, is_breaking


def fetch_pr(number: int) -> PR:
    """Fetch the title and body of a PR from GitHub."""
    output = gh("pr", "view", str(number), "--json", "title,body")
    data = json.loads(output)
    title = data["title"]
    kind, is_breaking = parse_title(title)
    return PR(number=number, title=title, body=data["body"], kind=kind, is_breaking=is_breaking)
