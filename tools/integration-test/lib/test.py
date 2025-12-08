# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Types used to define integration test cases.
"""
import os
import inspect
import importlib
from dataclasses import dataclass
from typing import Callable, List, Dict

from lib.proxy import CapturedRequest


class TestInput:
    """
    Input value provided to integration test entry point functions: provides the test
    with the full set of requests that were sent by the SDK during the test, while also
    providing a convenient interface for reading request data.
    """
    requests: List[CapturedRequest]

    def __init__(self, requests: List[CapturedRequest]):
        self.requests = requests

    def events(self, path: str) -> List[dict]:
        """
        Finds all POST requests that were sent to a URL with the given path, then
        examines those requests' bodies to parse.

        If no requests match the given path, or if all such requests have empty arrays
        for request bodies, returns an empty list.

        If any matching request has a body that is _not_ a valid JSON array containing
        JSON objects, raises ValueError.
        """
        events: List[dict] = []
        for request in self.requests:
            if request.method != 'POST' or request.url.path != path:
                continue
            json_body = request.json
            if not isinstance(json_body, list):
                raise ValueError(f'POST {path} has JSON body which is not an array value')
            for event in json_body:
                if not isinstance(event, dict):
                    raise ValueError(f'JSON array in body of POST {path} contains a non-object value')
            events.append(event)
        return events


AssertFunc = Callable[[TestInput], None]


@dataclass
class Test:
    name: str
    script: str
    assert_func: AssertFunc


def collect_tests() -> List[Test]:
    tests: List[Test] = []
    filenames_by_test_name: Dict[str, str] = {}

    tests_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'tests'))
    for filename in sorted(os.listdir(tests_dir)):
        # Only consider Python source files
        filename_noext, ext = os.path.splitext(filename)
        if ext.lower() != '.py':
            continue

        # Skip any hidden files, __init__.py, etc.
        if filename.startswith('.') or filename.startswith('__'):
            continue

        # Import the Python file as a module and check for a symbol named 'main'
        module = importlib.import_module(f'tests.{filename_noext}')
        if 'main' not in dir(module):
            raise ValueError('No main function defined in tests/%s' % filename)
        
        # Verify that 'main' is a function with the expected signature
        test_main = getattr(module, 'main')
        if not inspect.isfunction(test_main):
            raise ValueError('main value defined in tests/%s is not a function' % filename)
        sig = inspect.signature(test_main)
        if len(sig.parameters) != 1:
            raise ValueError('main function in tests/%s takes %d parameter(s); expected 1' % (filename, len(sig.parameters)))

        # Verify that 'main' has a docstring that encodes our test name and repl script
        doc = inspect.getdoc(test_main)
        if not doc:
            raise ValueError('main function in tests/%s has no docstring' % filename)
        doc_lines = doc.splitlines()

        # Docstring must start with a line containing the test name, optionally followed
        # by human-readable comments describing the test, followed by a '---' delimiter
        # line, then the listing of repl commands to run
        sections = _split_sections(doc_lines)
        if len(sections) != 2:
            raise ValueError('main function docstring in tests/%s has %d section(s) delimited by ---; expected 2' % (filename, len(sections)))
        desc_lines, script_lines = sections

        # First line of description section is taken as the name of the test
        name = desc_lines[0]
        if not name:
            raise ValueError('main function docstring in tests/%s does not specify test name as first line' % filename)
        
        # Script lines may include whitespace, comments, etc; strip them
        stripped_lines = [s.strip() for s in script_lines]
        script = '\n'.join([s for s in stripped_lines if s and not s.startswith('#')])
        if not script:
            raise ValueError('main function docstring in tests/%s does not specify any script commands' % filename)
        
        # Require every test to have a unique name
        other_filename = filenames_by_test_name.get(name)
        if other_filename:
            raise ValueError('duplicate test name "%s" used in both tests/%s and tests/%s' % (name, other_filename, filename))
        filenames_by_test_name[name] = filename

        # Construct a Test object and add it to our list
        tests.append(Test(name, script, test_main))

    # Return our accumulated list of Test objects
    return tests


def _split_sections(xs: List[str], delim_prefix: str = '---') -> List[List[str]]:
    sections: List[List[str]] = []
    current: List[str] = []
    for x in xs:
        if x.startswith(delim_prefix):
            if current:
                sections.append(current)
                current = []
        else:
            current.append(x)
    if current:
        sections.append(current)
    return sections
