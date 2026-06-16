# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for generating JSON schemas from dataclasses, for use in prompt.py.

By inspecting a dataclass and using the corresponding JSON schema to specify the output
format for an LLM prompt, we can submit a prompt + a dataclass and be reasonably certain
that the response from the LLM will be parseable as a value of that dataclass type.
"""
import ast
import dataclasses
import inspect
import textwrap
from typing import get_args, get_origin, get_type_hints


def _field_descriptions(cls: type) -> dict[str, str]:
    """Parse the source of `cls` and return any post-field string literals as descriptions."""
    source = textwrap.dedent(inspect.getsource(cls))
    tree = ast.parse(source)
    class_def = next(n for n in ast.walk(tree) if isinstance(n, ast.ClassDef))

    descriptions: dict[str, str] = {}
    for i, node in enumerate(class_def.body):
        if not (isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name)):
            continue
        nxt = class_def.body[i + 1] if i + 1 < len(class_def.body) else None
        if (
            nxt
            and isinstance(nxt, ast.Expr)
            and isinstance(nxt.value, ast.Constant)
            and isinstance(nxt.value.value, str)
        ):
            descriptions[node.target.id] = nxt.value.value

    return descriptions


def _type_to_schema(tp: type, seen: frozenset) -> dict:
    """Recursively convert a Python type annotation to a JSON schema fragment."""
    origin = get_origin(tp)
    if origin is list:
        args = get_args(tp)
        return {"type": "array", "items": _type_to_schema(args[0], seen) if args else {}}
    if tp is int:
        return {"type": "integer"}
    if tp is float:
        return {"type": "number"}
    if tp is bool:
        return {"type": "boolean"}
    if tp is str:
        return {"type": "string"}
    if dataclasses.is_dataclass(tp) and isinstance(tp, type):
        return _dataclass_schema(tp, seen)
    raise TypeError(f"Unsupported type: {tp}")


def _dataclass_schema(cls: type, seen: frozenset) -> dict:
    """Build a JSON schema object for a dataclass, with all fields required."""
    if cls in seen:
        raise ValueError(f"Circular reference: {cls}")
    seen = seen | {cls}

    hints = get_type_hints(cls)
    descriptions = _field_descriptions(cls)
    properties: dict[str, dict] = {}

    for field in dataclasses.fields(cls):
        schema = _type_to_schema(hints[field.name], seen)
        if field.name in descriptions:
            schema = {"description": descriptions[field.name], **schema}
        properties[field.name] = schema

    return {
        "type": "object",
        "properties": properties,
        "required": list(properties),
        "additionalProperties": False,
    }


def json_schema_from_dataclass(cls: type) -> dict:
    """Generate a JSON schema dict from a dataclass type, handling nested dataclasses."""
    return _dataclass_schema(cls, frozenset())
