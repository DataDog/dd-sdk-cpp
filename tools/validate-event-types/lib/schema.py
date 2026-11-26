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


def _resolve_schema_ids(schema_file_paths: List[str], base_dir: Path, store: Dict[str, Any]) -> List[str]:
    # Resolve each schema file to its "$id" (= its path relative to the base directory)
    schema_ids = []
    for schema_file_path in schema_file_paths:
        path = Path(schema_file_path).resolve()
        if not path.is_file():
            raise RuntimeError(f'No such file: {schema_file_path}')
        if not path.is_relative_to(base_dir):
            raise RuntimeError(f'{schema_file_path} is outside the base directory {base_dir}')
        schema_id = str(path.relative_to(base_dir))
        if schema_id in schema_ids:
            raise RuntimeError(f'{schema_file_path} is listed more than once')
        schema = store[schema_id]
        print(f'Schema title: {schema["title"]} ({schema_id})')

        # Verify that Draft7Validator is still the correct validator implementation
        if schema['$schema'] != 'http://json-schema.org/draft-07/schema':
            raise RuntimeError(f'{schema_id} uses schema "{schema["$schema"]}"; the jsonschema Validator class used in lib/schema.py may be out of date')
        schema_ids.append(schema_id)
    return schema_ids


@dataclass
class Schema:
    validator: Draft7Validator
    # "$id" of each schema file an event may match. Ordered: entry i is branch i of the
    # top-level "oneOf", and the first entry is the base for $ref resolution
    schema_ids: List[str]

    @classmethod
    def load(cls, schema_file_paths: List[str]) -> 'Schema':
        # Assume that all path references are relative to the directory containing the
        # first schema file; every other schema file must be within that directory
        if not schema_file_paths:
            raise RuntimeError('No schema files given')
        base_dir = Path(schema_file_paths[0]).resolve().parent
        print(f'Base directory for $ref resolution: {base_dir}')

        # Load the contents of all .json files within the base directory, and index them
        # by file path (relative to the base directory), which should match their "$id"
        store = _load_json_files(base_dir)
        print(f'Loaded {len(store)} .json files from base directory.')
        schema_ids = _resolve_schema_ids(schema_file_paths, base_dir, store)

        # Build a Registry containing the contents of all .json files within our base
        # directory, using relpath/"$id" as the URI for each file
        registry = Registry()
        for relpath, data in store.items():
            registry = registry.with_resource(relpath, Resource(data, DRAFT7))

        # Prepare a validator that accepts an event matching any of the schema files
        if len(schema_ids) == 1:
            top_schema = store[schema_ids[0]]
        else:
            top_schema = {
                '$schema': store[schema_ids[0]]['$schema'],
                'oneOf': [{'$ref': schema_id} for schema_id in schema_ids],
            }
        validator = Draft7Validator(top_schema, registry=registry)
        return cls(validator, schema_ids)

    def validate(self, obj: Dict[str, Any]):
        try:
            self.validator.validate(obj)
        except ValidationError as e:
            self._flatten_sub_errors(e)
            raise

    def _flatten_sub_errors(self, e: ValidationError):
        # With several schema files, the top-level "oneOf" over them would collapse all
        # errors against the first schema into a single opaque sub-error. Hoist that
        # sub-error's own sub-errors (with schema_path relative to the first schema, as if
        # it were validated alone), and label the other sub-errors with their "$id".
        if len(self.schema_ids) == 1:
            return
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
