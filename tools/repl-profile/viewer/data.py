# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for querying profiling data stored in a SQLite database.
"""
import os
import sqlite3
from datetime import datetime, timezone
from dataclasses import dataclass
from typing import List, Optional

import streamlit as st
import pandas as pd

__repl_profile_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
__sqlite_db_path__ = os.path.join(__repl_profile_root__, 'benchmark-results.db')


def db_exists() -> bool:
    return os.path.isfile(__sqlite_db_path__)


@st.cache_resource
def _get_connection() -> sqlite3.Connection:
    conn = sqlite3.connect(__sqlite_db_path__, check_same_thread=False)
    conn.execute('PRAGMA foreign_keys = ON')
    conn.row_factory = sqlite3.Row
    return conn


def list_revision_names() -> List[str]:
    cur = _get_connection().cursor()
    cur.execute('SELECT DISTINCT name FROM revision ORDER BY recorded_at')
    return [row['name'] for row in cur.fetchall()]


@st.cache_data
def list_benchmark_names() -> List[str]:
    cur = _get_connection().cursor()
    cur.execute('SELECT DISTINCT benchmark_name FROM invocation ORDER BY benchmark_name, recorded_at')
    return [row['benchmark_name'] for row in cur.fetchall()]


@st.cache_data
def list_platforms() -> List[str]:
    cur = _get_connection().cursor()
    cur.execute('SELECT DISTINCT platform FROM invocation ORDER BY benchmark_name, recorded_at')
    return [row['platform'] for row in cur.fetchall()]


@st.cache_data
def list_build_configs() -> List[str]:
    cur = _get_connection().cursor()
    cur.execute('SELECT DISTINCT build_config FROM invocation ORDER BY benchmark_name, recorded_at')
    return [row['build_config'] for row in cur.fetchall()]


@dataclass
class Invocation:
    id: int
    recorded_at: datetime
    setup_duration_mean: float
    setup_duration_median: float
    setup_net_bytes: int
    teardown_duration_mean: float
    teardown_duration_median: float
    teardown_net_bytes: int


@st.cache_data
def find_invocation(benchmark_name: str, revision_name: str, platform: str, build_config: str) -> Optional[Invocation]:
    cur = _get_connection().cursor()
    cur.execute('''
        SELECT
            id,
            recorded_at,
            setup_duration_mean,
            setup_duration_median,
            setup_net_bytes,
            teardown_duration_mean,
            teardown_duration_median,
            teardown_net_bytes
        FROM invocation
        WHERE benchmark_name = ?
            AND revision_name = ?
            AND platform = ?
            AND build_config = ?
    ''', (benchmark_name, revision_name, platform, build_config))
    row = cur.fetchone()
    if not row:
        return None
    return Invocation(
        id=row['id'],
        recorded_at=datetime.fromtimestamp(row['recorded_at'] / 1000, tz=timezone.utc),
        setup_duration_mean=row['setup_duration_mean'],
        setup_duration_median=row['setup_duration_median'],
        setup_net_bytes=row['setup_net_bytes'],
        teardown_duration_mean=row['teardown_duration_mean'],
        teardown_duration_median=row['teardown_duration_median'],
        teardown_net_bytes=row['teardown_net_bytes'],
    )


@st.cache_data
def load_commands(invocation_id: int) -> pd.DataFrame:
    query = """
        SELECT
            c.id AS command_id,
            c.label AS label,
            c.duration_median AS duration_median,
            c.duration_mean AS duration_mean,
            c.duration_stddev AS duration_stddev,
            c.duration_sem AS duration_sem,
            c.duration_iqr AS duration_iqr,
            c.duration_ci95_lo AS duration_ci95_lo,
            c.duration_ci95_hi AS duration_ci95_hi,
            COUNT(CASE WHEN ae.is_alloc = 1 THEN 1 END) AS num_allocs,
            COUNT(CASE WHEN ae.is_alloc = 0 THEN 1 END) AS num_frees,
            COALESCE(SUM(CASE WHEN ae.is_alloc = 1 THEN ae.size ELSE 0 END), 0) AS total_alloc,
            COALESCE(SUM(CASE WHEN ae.is_alloc = 0 THEN ae.size ELSE 0 END), 0) AS total_free,
            COALESCE(SUM(CASE WHEN ae.is_alloc = 1 THEN ae.size ELSE -ae.size END), 0) AS net_bytes,
            MIN(CASE WHEN ae.is_alloc = 1 THEN ae.size END) AS min_alloc_size,
            MAX(CASE WHEN ae.is_alloc = 1 THEN ae.size END) AS max_alloc_size
        FROM command c
        LEFT JOIN alloc_event ae ON ae.command_id = c.id
        WHERE c.invocation_id = ?
        GROUP BY c.id, c.label
        ORDER BY c.id
    """
    return pd.read_sql_query(query, _get_connection(), params=(invocation_id,))


@st.cache_data
def load_allocations(command_id: int) -> pd.DataFrame:
    query = """
        SELECT
          is_alloc,
          size,
          thread_index
        FROM alloc_event ae
        WHERE ae.command_id = ?
    """
    return pd.read_sql_query(query, _get_connection(), params=(command_id,))
