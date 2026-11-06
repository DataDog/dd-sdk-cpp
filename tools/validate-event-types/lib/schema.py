# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for loading a JSON schema given multiple files with $id and $ref links.
"""
import json
from pathlib import Path
from collections import deque
from dataclasses import dataclass
from typing import Dict, Any, List

from jsonschema import RefResolver, Draft7Validator
from jsonschema.exceptions import ValidationError
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT7

# Matches the "$id" of standalone timeseries event schemas (e.g. timeseries-cpu-schema.json)
# that rum-events-format does not include in the root schema's "oneOf"
_TIMESERIES_SCHEMA_GLOB = 'rum/timeseries-*-schema.json'


def _load_json_files(base_dir: Path) -> Dict[str, Any]:
    store: Dict[str, Any] = {}
    for path in sorted(base_dir.rglob('*.json')):
        with open(path, 'r') as fp:
            data = json.load(fp)
        
        relpath = str(path.relative_to(base_dir))
        file_id = data['$id']
        assert relpath == file_id

        store[relpath] = data
    return store


@dataclass
class Schema:
    validator: Draft7Validator
    # "$id" of each schema in the combined schema's "oneOf", by index; the first entry
    # is the root schema
    schema_ids: List[str]

    @classmethod
    def load(cls, root_file_path: str) -> 'Schema':
        # Ensure that our root schema file exists, and assume that all path references
        # are relative to the directory containing that file
        if not Path(root_file_path).is_file():
            raise RuntimeError(f'No such file: {root_file_path}')
        base_dir = Path(root_file_path).parent
        print(f'Base directory for $ref resolution: {base_dir}')

        # Load the contents of all .json files within the base directory, and index them
        # by file path (relative to the base directory), which should match their "$id"
        store = _load_json_files(base_dir)
        print(f'Loaded {len(store)} .json files from base directory.')

        # We should have loaded the root schema file
        root_file_name = Path(root_file_path).name
        assert root_file_name in store
        root_schema = store[root_file_name]
        print(f'Root schema title: {root_schema["title"]}')

        # Verify that Draft7Validator is still the correct validator implementation
        if root_schema['$schema'] != 'http://json-schema.org/draft-07/schema':
            raise RuntimeError(f'{root_file_name} uses schema "{root_schema["$schema"]}"; the jsonschema Validator class used in lib/schema.py may be out of date')

        # Build a Registry containing the contents of all .json files within our base
        # directory, using relpath/"$id" as the URI for each file
        registry = Registry()
        for relpath, data in store.items():
            registry = registry.with_resource(relpath, Resource(data, DRAFT7))

        # Timeseries events have their own top-level schemas which are not referenced by
        # the root schema, so accept any event matching either the root schema or one of
        # the timeseries schemas
        timeseries_ids = sorted(relpath for relpath in store if Path(relpath).match(_TIMESERIES_SCHEMA_GLOB))
        for schema_id in timeseries_ids:
            print(f'Additional schema title: {store[schema_id]["title"]} ({schema_id})')
        schema_ids = [root_file_name, *timeseries_ids]
        combined_schema = {
            '$schema': root_schema['$schema'],
            'oneOf': [{'$ref': schema_id} for schema_id in schema_ids],
        }

        # Prepare a validator that can check JSON values against our combined schema
        validator = Draft7Validator(combined_schema, registry=registry)
        return cls(validator, schema_ids)

    def validate(self, obj: Dict[str, Any]):
        try:
            self.validator.validate(obj)
        except ValidationError as e:
            self._flatten_sub_errors(e)
            raise

    def _flatten_sub_errors(self, e: ValidationError):
        # The combined schema wraps the root schema in an extra "oneOf", which would
        # collapse all errors against the root schema into a single opaque sub-error.
        # Hoist that sub-error's own sub-errors (one per root event type, with
        # schema_path relative to the root schema, as if the root schema were validated
        # directly), and label the other sub-errors with the "$id" of their schema.
        if e.validator != 'oneOf' or e.relative_schema_path != deque(['oneOf']):
            return
        context = []
        for sub_error in e.context:
            index = sub_error.relative_schema_path[0]
            if index == 0 and sub_error.validator == 'oneOf' and sub_error.context:
                nested = sub_error.context
            else:
                sub_error.relative_schema_path[0] = self.schema_ids[index]
                nested = [sub_error]
            for error in nested:
                error.parent = e
            context.extend(nested)
        e.context = context
