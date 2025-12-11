# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Streamlit app for viewing historical profiling data from benchmark-results.db.

From the root of this repository:

  python -m venv tools/repl-profile/.venv
  source tools/repl-profile/.venv/bin/activate
  pip install -r requirements.txt
  python -m streamlit tools/repl-profile/app.py
"""
import re
import sys
from typing import List, Optional, Dict, Any

import streamlit as st

from viewer.data import (
    Invocation,
    db_exists,
    list_revision_names,
    list_benchmark_names,
    list_platforms,
    list_build_configs,
    find_invocation,
    load_allocations,
)
from viewer.plot import (
    load_invocation_data,
    plot_command_durations,
    plot_command_allocations,
    plot_heap_timeline,
)


def _default_platform_index(names: List[str]) -> int:
    try:
        return names.index(sys.platform)
    except ValueError:
        return 0


def _default_build_config_index(names: List[str]) -> int:
    try:
        return names.index('release')
    except ValueError:
        return 0


def _newest_semver_index(names: List[str]) -> int:
    result = -1
    for i, name in enumerate(names):
        if re.compile(r'\d+\.\d+\.\d+').match(name):
            result = i
    return result


def _default_baseline_revision_index(names: List[str]) -> int:
    i = _newest_semver_index(names)
    if i >= 0:
        return i
    if len(names) > 1:
        return len(names) - 2
    return 0


def _default_test_revision_index(names: List[str]) -> int:
    if len(names) > 0:
        return len(names) - 1
    return 0


def main():
    st.set_page_config(
        page_title="Benchmark Viewer",
        layout="wide",
    )

    # Hit the db for the distinct set of revisions, benchmarks, platforms, and build
    # configurations as represented in the current set of profiling data
    has_db = db_exists()
    revision_names = list_revision_names() if has_db else []
    benchmark_names = list_benchmark_names() if has_db else []
    platform_names = list_platforms() if has_db else []
    build_config_names = list_build_configs() if has_db else []

    # If we don't have any data loaded, abort
    if not revision_names or not benchmark_names or not platform_names or not build_config_names:
        st.title('No profiling data available')
        st.markdown('Insufficient data. Run `tools/repl-profile/main.py` to populate `benchmark-results.db`, then choose a revision to view.')
        return

    # Render controls in the sidebar for selecting the desired benchmark, platform, and
    # build configuration
    benchmark_name = st.sidebar.selectbox('Benchmark', benchmark_names)
    platform = st.sidebar.selectbox(
        'Platform',
        platform_names,
        index=_default_platform_index(platform_names),
    )
    build_config = st.sidebar.selectbox(
        'Build Config',
        build_config_names,
        index=_default_build_config_index(build_config_names),
    )

    # Render controls for selecting two revisions of the SDK: one to treat as the
    # baseline, and another as the revision under test that we're comparing against that
    # baseline
    baseline_revision_name = st.sidebar.selectbox(
        'Baseline Revision',
        revision_names,
        index=_default_baseline_revision_index(revision_names),
    )
    test_revision_name = st.sidebar.selectbox(
        'Test Revision',
        revision_names,
        index=_default_test_revision_index(revision_names),
    )

    # Hit the db to see whether it contains profiling data for the configured revision,
    # platform, build config, and benchmark; resolving the invocation ID if found
    baseline_invocation = find_invocation(benchmark_name, baseline_revision_name, platform, build_config)
    test_invocation = find_invocation(benchmark_name, test_revision_name, platform, build_config)

    # If neither of our selected revisions has any profiling data for our chosen
    # configuration, abort
    if not baseline_invocation and not test_invocation:
        subj_verb = ('revision has' if baseline_revision_name == test_revision_name else 'revisions have')
        st.title(f'{benchmark_name}: n/a')
        st.markdown(f'The selected {subj_verb} no profiling data recorded for the `{benchmark_name}` benchmark in the selected configuration (`{platform}` | `{build_config}`).')
        return

    # Detect whether we only have one revision selected, in which case we'll display its
    # data by itself, without comparison
    single_invocation: Optional[Invocation] = None
    single_revision_name = ''
    if not baseline_invocation:
        single_invocation = test_invocation
        single_revision_name = test_revision_name
    elif not test_invocation:
        single_invocation = baseline_invocation
        single_revision_name = baseline_revision_name
    elif baseline_invocation.id == test_invocation.id:
        single_invocation = baseline_invocation
        single_revision_name = baseline_revision_name

    # Add 'revision' and 'sequence_num' values to all series; and concatenate baseline
    # and test into a single DataFrame if we have both
    if single_invocation:
        st.title(f'{benchmark_name}: {single_invocation.id}')
        data = load_invocation_data((single_revision_name, single_invocation))
    else:
        st.title(f'{benchmark_name}: {baseline_invocation.id} vs. {test_invocation.id}')
        assert baseline_invocation and test_invocation
        assert baseline_invocation.id != test_invocation.id
        data = load_invocation_data((baseline_revision_name, baseline_invocation), (test_revision_name, test_invocation))

    # Prepare on-select callbacks so we can record which chart was most recently clicked
    # on, for resolving selected command id
    def _on_select_command_durations():
        st.session_state['last_selection_source'] = 'command_durations'
    def _on_select_command_allocations():
        st.session_state['last_selection_source'] = 'command_allocations'
    def _on_select_heap_timeline():
        st.session_state['last_selection_source'] = 'heap_timeline'

    # Examine current session state to resolve whether we have a specific command
    # execution selected: this is the case if our last-selected chart has a current
    # selection of exactly one data point
    selected_command_id = 0
    selected_command_label = ''
    last_source = st.session_state.get('last_selection_source')
    if last_source:
        chart = st.session_state.get(last_source, {})
        if chart and chart['selection'] and len(chart['selection']['points']) == 1:
            point = chart['selection']['points'][0]
            if point['customdata'] and isinstance(point['customdata'][-1], int):
                selected_command_id = point['customdata'][-1]
                selected_command_label = point['customdata'][0]

    # Split the main view into two columns
    left, right = st.columns(2, vertical_alignment='top')

    # Top-left: Main-thread CPU time per command
    left.plotly_chart(plot_command_durations(data), on_select=_on_select_command_durations, key='command_durations')

    # Top-right: Number of malloc() calls per command
    right.plotly_chart(plot_command_allocations(data), on_select=_on_select_command_allocations, key='command_allocations')

    # Bottom-right: Heap usage over time
    right.plotly_chart(plot_heap_timeline(data), on_select=_on_select_heap_timeline, key='heap_timeline')

    # Bottom-left: detailed breakdown of allocation events for whichever command was
    # most recently clicked in any chart
    detail = left.container()
    if selected_command_id == 0:
        detail.markdown('#### Command Allocations')
        detail.markdown('Click a point in any of the surrounding graphs to view detailed allocation stats for that command execution.')
    else:
        detail.markdown(f'#### {selected_command_label} Allocations [{selected_command_id}]')
        detail.write(load_allocations(selected_command_id))


if __name__ == "__main__":
    main()
