# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for reading the source of the C++ SDK's unit tests to discover JSON object
values that should be validated as RUM events.
"""
import os
import re
import json
from dataclasses import dataclass
from typing import List, Dict, Any

__cpp_tests_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', 'tests'))
assert os.path.isdir(__cpp_tests_root__)

__cpp_assert_func_name__ = 'RequireValidRumEvent'
__cpp_assert_invocation_regex__ = re.compile(__cpp_assert_func_name__ + r'\([a-zA-Z0-9_]+,\s+')
__cpp_raw_string_literal_open__ = 'R"('
__cpp_raw_string_literal_close__ = ')"'


@dataclass
class EventLiteral:
    location: str
    data: Dict[str, Any]


def discover_rum_event_literals(relpaths: List[str]) -> List[EventLiteral]:
    # Iterate over all specified test files, collecting literal JSON object payloads
    # passed to RequireValidRumEvent()
    events: List[EventLiteral] = []
    for relpath in relpaths:
        # Verify that file exists within this repo's tests/ dir
        abspath = os.path.join(__cpp_tests_root__, relpath)
        if not os.path.isfile(abspath):
            raise RuntimeError(f'No such file: {abspath}')

        # Read the contents of the .cpp test file
        with open(abspath, 'r') as fp:
            cpp_source = fp.read()

        # Iterate over all invocations of RequireValidRumEvent, the use of which signals
        # to this script that the literal JSON object provided as the second parameter
        # is expected to conform to the RUM events schema
        prev_string_value_end_pos = -1
        for match in re.finditer(__cpp_assert_invocation_regex__, cpp_source):
            # Compute line number and print location
            line_num = cpp_source[:match.start()].count('\n') + 1
            location = f'{os.path.join("tests", relpath)}:{line_num}'
            print(f'- {location}')

            # If this is not our first match, it should start after the end of the
            # string literal value identified from the previous match
            if prev_string_value_end_pos >= 0 and match.start() <= prev_string_value_end_pos:
                raise RuntimeError(f'String literal values overlap')

            # The text immediately following the regex match should be a C++ raw string
            # literal
            string_value_start = match.end()+len(__cpp_raw_string_literal_open__)
            if cpp_source[match.end():string_value_start] != __cpp_raw_string_literal_open__:
                raise RuntimeError(f'Second argument to {__cpp_assert_func_name__} is not a raw-string literal (i.e. `{__cpp_raw_string_literal_open__}`)')
            string_value_end = cpp_source.index(__cpp_raw_string_literal_close__, string_value_start)
            if string_value_end < 0:
                raise RuntimeError(f'String literal value is not closed')
            prev_string_value_end_pos = string_value_end

            # The text within the raw-string literal should be a valid JSON object:
            # parse it
            string_value = cpp_source[string_value_start:string_value_end]
            try:
                data = json.loads(string_value)
            except json.JSONDecodeError:
                print(string_value)
                raise
            if not isinstance(data, dict):
                raise RuntimeError('JSON value is not an object literal')
            
            # This is well-formed JSON object; we can collect it to be validated against
            # our schema
            events.append(EventLiteral(location, data))

    return events
