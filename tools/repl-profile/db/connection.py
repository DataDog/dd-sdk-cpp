# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Common code for connecting to the database stored at repl-profile/benchmark-results.db.
"""
import os
import sqlite3

__repl_profile_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
__sqlite_db_path__ = os.path.join(__repl_profile_root__, 'benchmark-results.db')


def db_connect() -> sqlite3.Connection:
    conn = sqlite3.connect(__sqlite_db_path__)
    conn.execute('PRAGMA foreign_keys = ON')
    conn.row_factory = sqlite3.Row
    return conn
