# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for applying database migrations to keep the schema used in benchmark-results.db up
to date.
"""
import os
import re
import sqlite3
from typing import List

from db.connection import db_connect

__migrations_root__ = os.path.join(os.path.dirname(__file__), 'migrations')
__migration_filename_regex__ = re.compile(r'^(\d{3})_[A-Za-z0-9_-]+\.sql$')


def _list_migrations() -> List[str]:
    filenames: List[str] = []
    for name in sorted(os.listdir(__migrations_root__)):
        if not os.path.isfile(os.path.join(__migrations_root__, name)):
            continue
        if not __migration_filename_regex__.match(name):
            continue
        filenames.append(name)
    return filenames


def _create_migration_table(conn: sqlite3.Connection):
    cur = conn.cursor()
    cur.execute('CREATE TABLE IF NOT EXISTS migrations (version INTEGER NOT NULL)')
    conn.commit()


def _get_current_migration_version(conn: sqlite3.Connection) -> int:
    cur = conn.cursor()
    cur.execute('SELECT COALESCE(MAX(version), 0) FROM migrations')
    row = cur.fetchone()
    return row[0]


def _update_migration_version(conn: sqlite3.Connection, version: int):
    cur = conn.cursor()
    cur.execute('DELETE FROM migrations')
    cur.execute('INSERT INTO migrations (version) VALUES (?)', (version,))
    conn.commit()


def db_migrate():
    # Check migrations/*.sql to determine our latest db schema version
    filenames = _list_migrations()
    if not filenames:
        raise RuntimeError(f'No db migrations found in {__migrations_root__}')
    target_version = max(int(__migration_filename_regex__.match(s).group(1)) for s in filenames)

    # Connect to the db (opening our local benchmark-results.db file) and read the
    # version number stored in our 'migrations' table
    with db_connect() as conn:
        _create_migration_table(conn)
        current_version = _get_current_migration_version(conn)

        # If we need to upgrade, execute the contents of each migration file until we've
        # reached the latest version
        if current_version > target_version:
            raise RuntimeError(f'db is at version {current_version}; latest migration is {target_version}')
        if current_version == target_version:
            print(f'db is up to date at version {current_version}')
            return
        if current_version < target_version:
            print(f'Upgrading from db version {current_version} to version {target_version}...')
            for version in range(current_version + 1, target_version + 1):
                filename = next(s for s in filenames if int(__migration_filename_regex__.match(s).group(1)) == version)
                print(f'- {filename}')
                with open(os.path.join(__migrations_root__, filename)) as fp:
                    migration_sql = fp.read()
                conn.executescript(migration_sql)
                _update_migration_version(conn, version)
                conn.commit()
                print(f'Now at version {version}.')
