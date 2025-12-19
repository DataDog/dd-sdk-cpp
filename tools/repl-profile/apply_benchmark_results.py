# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
repl-profile/apply_benchmark_results.py
Usage: python3 tools/repl-profile/apply_benchmark_results.py
Requires no external dependencies; any Python 3.10+ interpreter will work.

Accepts multiple input .sql files produced by run_benchmark.py (with -o <file>), then
commits them to benchmark-results.db.
"""
import argparse

from db.connection import db_connect
from db.migrations import db_migrate


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('files', metavar='FILE', nargs='+', help='One or more .sql input files to execute against benchmark-results.db')
    args = parser.parse_args()

    print('Applying .sql data to benchmark-results.db...')
    db_migrate()
    with db_connect() as conn:
        cur = conn.cursor()
        for filename in args.input_files:
            print(f'Executing {filename}...')
            with open(filename) as fp:
                sql_script = fp.read()
            cur.executescript(sql_script)
        conn.commit()
    
    print('Wrote results to local database file.')
