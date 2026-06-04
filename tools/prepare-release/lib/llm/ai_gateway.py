# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Setup code for making API requests via the Datadog AI Gateway.
"""
import os
import subprocess
import anthropic


def _get_ai_gateway_token() -> str:
    """Returns a Datadog-internal auth token with `aud: rapid-ai-platform`, suitable for
    use as a bearer token in calls to Datadog's AI Gateway."""
    # In CI, we use the authanywhere utility to obtain a token from the GitLab OIDC:
    # this value should already be set by in the Bash script for the CI job
    env_token = os.environ.get("AI_GATEWAY_TOKEN")
    if env_token:
        return env_token
    
    # When testing locally on Datadog developer machines, use ddtool
    return subprocess.check_output(
        ["ddtool", "auth", "token", "rapid-ai-platform", "--datacenter", "us1.ddbuild.io"],
        text=True,
    ).strip()


def make_ai_gateway_client() -> anthropic.Anthropic:
    """Prepares an Anthropic API client properly configured for AI Gateway auth."""
    # Pass a dummy api_key since the Anthropic client requires it; actual auth is via
    # the Authorization header, which uses a Datadog-issued auth token
    return anthropic.Anthropic(
        api_key="dd-gateway",
        base_url="https://ai-gateway.us1.ddbuild.io",
        default_headers={
            "Authorization": f"Bearer {_get_ai_gateway_token()}",
            "provider": "anthropic",
            "source": "claude-code",
            "org-id": "2",
        },
    )
