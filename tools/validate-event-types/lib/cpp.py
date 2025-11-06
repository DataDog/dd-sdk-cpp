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
import uuid
from dataclasses import dataclass
from typing import List, Dict, Any

__cpp_tests_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', 'tests'))
assert os.path.isdir(__cpp_tests_root__)

__cpp_macro_name__ = 'DATADOG_RUM_EVENT_LITERAL'
__cpp_macro_regex__ = re.compile(__cpp_macro_name__ + r'\(')
__cpp_raw_string_literal_open__ = 'R"('
__cpp_raw_string_literal_close__ = ')"'

__template_var_regex__ = re.compile(r'"\${__(.+)__}"')
__template_substitutions__ = {
    'NONZERO_UUID': lambda: json.dumps(str(uuid.uuid4())),
}

def _apply_template_substitutions(s: str) -> str:
    def _replace(match: re.Match) -> str:
        var_name = match.group(1)
        value_thunk = __template_substitutions__.get(var_name)
        if not value_thunk:
            raise RuntimeError(f'Invalid template var {var_name}')
        return value_thunk()
    return re.sub(__template_var_regex__, _replace, s)


@dataclass
class EventLiteral:
    location: str
    data: Dict[str, Any]


def discover_rum_event_literals(relpaths: List[str]) -> List[EventLiteral]:
    # Iterate over all specified test files, collecting literal JSON object payloads
    # wrapped in DATADOG_RUM_EVENT_LITERAL()
    events: List[EventLiteral] = []
    for relpath in relpaths:
        # Verify that file exists within this repo's tests/ dir
        abspath = os.path.join(__cpp_tests_root__, relpath)
        if not os.path.isfile(abspath):
            raise RuntimeError(f'No such file: {abspath}')

        # Read the contents of the .cpp test file
        with open(abspath, 'r') as fp:
            cpp_source = fp.read()

        # Keep track of matches in this file: we'll validate
        num_matches_in_this_file = 0

        # Iterate over all invocations of DATADOG_RUM_EVENT_LITERAL, which signals to
        # this script that its argument is a JSON literal that we expect to conform to
        # the RUM event schema
        prev_string_value_end_pos = -1
        for match in re.finditer(__cpp_macro_regex__, cpp_source):
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
                raise RuntimeError(f'Argument to {__cpp_macro_name__} is not a raw-string literal (i.e. `{__cpp_raw_string_literal_open__}`)')
            string_value_end = cpp_source.index(__cpp_raw_string_literal_close__, string_value_start)
            if string_value_end < 0:
                raise RuntimeError(f'String literal value is not closed')
            prev_string_value_end_pos = string_value_end

            # The text within the raw-string literal should describe a valid JSON
            # object, possibly with template variables specified as string values for
            # some properties (e.g. `"${__NONZERO_UUID__}"`): perform template
            # substitution on the string before parsing it as JSON
            template_string_value = cpp_source[string_value_start:string_value_end]
            string_value = _apply_template_substitutions(template_string_value)

            # We should now have a literal JSON object: parse it
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
            num_matches_in_this_file += 1

        # Fail hard if any file we're configured to search doesn't contain any
        # invocations of DATADOG_RUM_EVENT_LITERAL
        if num_matches_in_this_file == 0:
            raise RuntimeError(f'No uses of {__cpp_macro_name__} found in {relpath}')

    # Return event literals collected from all files
    return events
