# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for examining CMake files to ascertain details about the configuration of local
builds of the SDK.
"""
import os
import re
from dataclasses import dataclass
from typing import Dict, Optional

__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
__cmake_cache_var_regex__ = re.compile(r'^([A-Za-z0-9_-]+):([A-Z]+)=(.*)$')


@dataclass
class CMakeCacheVar:
    type: str
    value: str


@dataclass
class CMakeCache:
    vars: Dict[str, CMakeCacheVar]

    def get(self, name: str) -> str:
        var = self.vars.get(name)
        return var.value if var else ''

    def has_flag(self, name: str) -> bool:
        return self.get(name).lower() not in ('', 'off', 'false', 'no', 'n', '0')


def load_cmake_cache() -> Optional[CMakeCache]:
    filepath = os.path.join(__repo_root__, 'build', 'CMakeCache.txt')
    if not os.path.isfile(filepath):
        return None
    
    vars: Dict[str, CMakeCacheVar] = {}
    with open(filepath) as fp:
        for line in fp:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#') or line.startswith('//'):
                continue
            match = __cmake_cache_var_regex__.match(line)
            if match:
                vars[match.group(1)] = CMakeCacheVar(match.group(2), match.group(3))
    return CMakeCache(vars)
