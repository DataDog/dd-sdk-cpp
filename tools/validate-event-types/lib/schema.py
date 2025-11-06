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
from dataclasses import dataclass
from typing import Dict, Any

from jsonschema import RefResolver, Draft7Validator
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


@dataclass
class Schema:
    validator: Draft7Validator

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

        # Prepare a validator that can check JSON values against our root schema
        validator = Draft7Validator(root_schema, registry=registry)
        return cls(validator)

    def validate(self, obj: Dict[str, Any]):
        self.validator.validate(obj)
