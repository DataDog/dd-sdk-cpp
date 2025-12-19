# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for generating SQL scripts.

In CI, we may run multiple parallel profiling jobs, then run a final job that updates
the database file with canonical profiling data. To facilitate merging multiple sets of
results into the DB, we perform this recording in two steps: first we generate a SQL
file with all the data to be inserted/updated, then we execute one or more such scripts
when we're ready to modify benchmark-results.db.
"""
from typing import Tuple, Any, List


def _sql_string_literal(s: str) -> str:
    return "'%s'" % s.replace("'", "''")


def _sql_format(query: str, args: Tuple[Any]) -> str:
    # Normalize whitespace to condense the query to a single line, and ensure it ends
    # with a semicolon
    query = ' '.join(query.split())
    if not query.endswith(';'):
        query += ';'

    # Count the number of literal question marks in our query: any occurrence of '?' is
    # assumed to be a positional placeholder
    num_placeholders = query.count('?')
    if num_placeholders != len(args):
        raise ValueError(f'Query has {num_placeholders} placeholder(s); {len(args)} argument values given')
    if num_placeholders == 0:
        return query

    # Render a SQL string literal for each argument value
    literals: List[str] = []
    for arg in args:
        if isinstance(arg, str):
            literals.append(_sql_string_literal(arg))
        elif isinstance(arg, int):
            literals.append(repr(arg))
        elif isinstance(arg, bool):
            literals.append(repr(int(arg)))
        elif isinstance(arg, float):
            literals.append(repr(arg))
        else:
            raise TypeError(f'Invalid type SQL argument type {type(arg)}')
    
    # Iteratively replace '?' placeholders with their corresponding value literal
    for literal in literals:
        query = query.replace('?', literal, 1)
    assert query.count('?') == 0

    # Return the formatted SQL statement
    return query


class SqlWriter:
    data: str = ''

    def write(self, query: str, *args: Any):
        line = _sql_format(query, args)
        self.data += f'{line}\n'
