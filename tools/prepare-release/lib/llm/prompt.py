# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for running LLM prompts with structured output formats.
"""
import dataclasses
import json
import shutil
import textwrap
from typing import TypeVar, get_args, get_origin, get_type_hints

import anthropic

from .schema import json_schema_from_dataclass
from .costs import record_llm_call_costs

T = TypeVar("T")

_DEFAULT_MODEL = "claude-sonnet-4-6"


def _from_dict(tp: type, value):
    """Recursively deserialize a parsed JSON value into an instance of `tp`."""
    origin = get_origin(tp)
    if origin is list:
        item_type = get_args(tp)[0]
        return [_from_dict(item_type, item) for item in value]
    if dataclasses.is_dataclass(tp) and isinstance(tp, type):
        hints = get_type_hints(tp)
        kwargs = {f.name: _from_dict(hints[f.name], value[f.name]) for f in dataclasses.fields(tp)}
        return tp(**kwargs)
    return value


def _print_debug_prompt(prompt: str):
    width = shutil.get_terminal_size().columns
    wrapped = "\n".join(textwrap.fill(line, width=width) for line in prompt.splitlines())
    print(f"\033[90m{wrapped}\033[0m")


def run_structured_prompt(
    client: anthropic.Anthropic,
    prompt: str,
    cls: type[T],
    model: str = _DEFAULT_MODEL,
    max_tokens: int = 4096,
    debug_print: bool = False,
) -> T:
    """Send `prompt` to the model and return a structured response deserialized as `cls`."""
    if debug_print:
        _print_debug_prompt(prompt)
    response = client.messages.create(
        model=model,
        max_tokens=max_tokens,
        output_config={
            "format": {
                "type": "json_schema",
                "schema": json_schema_from_dataclass(cls),
            },
        },
        messages=[{"role": "user", "content": prompt}],
    )
    record_llm_call_costs(response, cls.__name__)
    return _from_dict(cls, json.loads(response.content[0].text))
