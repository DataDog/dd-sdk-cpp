# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
validate-event-types
Usage: python3 tools/validate-event-types/main.py [rum-events-format-commit-ref]

Running this script will:

1. Prepare a local clone of the rum-events-format repo at the specified revision
   (latest if no revision specified)
2. Use python-jsonschema to parse rum-events-format/schemas/rum-events-schema.json
3. Find every invocation of the `DATADOG_RUM_EVENT_LITERAL` macro in the subset of .cpp
   test files configured below, each of which must wrap a C++ raw-string literal
   representing the exact value of a RUM event's JSON object literal
4. Use python-jsonschema to validate that JSON object literal against the schema

This validation process does not directly exercise the functionality of the C++ SDK. The
C++ unit test is responsible for validating that the SDK produces the expected JSON
payload exactly as shown in the `DATADOG_RUM_EVENT_LITERAL` call. This script separately
validates that the expected JSON payload is indeed a valid RUM event according to the
schema. If both hold true, we know that the C++ SDK produces valid RUM events within the
extent of our test coverage.
"""
import os
import json
import argparse

from jsonschema.exceptions import ValidationError

from lib.git import fetch_repo
from lib.schema import Schema
from lib.cpp import discover_rum_event_literals

__schema_repo__ = 'rum-events-format'
__schema_repo_default_branch__ = 'master'
__schema_relpath__ = os.path.join('schemas', 'rum-events-schema.json')
__schema_test_event_relpath__ = os.path.join('samples', 'rum-events', 'view.json')

__cpp_test_relpaths__ = [
    os.path.join('impl', 'types', 'rum_test.cpp'),
    os.path.join('impl', 'crash_processing', 'crash_handling_test.cpp'),
    os.path.join('impl', 'crash_processing', 'view_event_mutation_test.cpp'),
    os.path.join('c', 'rum_c_api_test.cpp'),
    os.path.join('cpp', 'rum_cpp_api_test.cpp'),
]


def print_section(s: str):
    print(f'\n=== {s} ===')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('commit_ref', nargs='?', default=__schema_repo_default_branch__, help=f'branch, tag, or commit sha for {__schema_repo__}')
    args = parser.parse_args()

    print('validate-event-types')
    print(f'- commit_ref: {args.commit_ref}')

    # Ensure that we have the rum-events-format repo cloned at the desired revision
    print_section(f'Fetching {__schema_repo__} @ {args.commit_ref}...')
    repo = fetch_repo(__schema_repo__, args.commit_ref)
    print(f'{repo.working_dir} @ {repo.head.commit}')

    # Load rum-events-schema.json from that repo
    print_section(f'Parsing {__schema_relpath__}')
    root_schema_json_path = os.path.join(repo.working_dir, __schema_relpath__)
    schema = Schema.load(root_schema_json_path)

    # Sanity-check: validate an example event from the rum-events-format repo
    print_section(f'Validating {__schema_test_event_relpath__}')
    with open(os.path.join(repo.working_dir, __schema_test_event_relpath__), 'r') as fp:
        test_event_data = json.load(fp)
    schema.validate(test_event_data)
    print('Sanity check passed: canonical event is validated OK.')

    # Examine .cpp tests to find usages of DATADOG_RUM_EVENT_LITERAL, then parse
    # expected RUM event payloads from their arguments
    print_section(f'Discovering RUM event literals in .cpp test files')
    event_literals = discover_rum_event_literals(__cpp_test_relpaths__)
    if not event_literals:
        raise RuntimeError('Failed to resolve any RUM event literals in .cpp tests')
    print(f'Found {len(event_literals)} RUM event literals to validate.')

    # Perform JSON schema validation for each event payload, raising an error if
    # validation fails
    print_section(f'Validating JSON event literals used in .cpp test files')
    for event in event_literals:
        try:
            schema.validate(event.data)
            print(f'✅ {event.location}')
        except ValidationError as e:
            print(f'❌ {event.location}')
            print("Error context (sub-errors):", len(e.context))
            for sub_error in e.context:
                print(f"  - {list(sub_error.schema_path)}: {sub_error.message}")
            raise
    
    # If we didn't hit any errors, then we had at least one event, and all events are
    # valid
    print('Done.')
