# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for generating plots from profiling data.
"""
from dataclasses import dataclass
from typing import Optional, Dict, Tuple

from viewer.data import Invocation, load_commands

import pandas as pd
import plotly.express as px
import plotly.graph_objects as go


@dataclass
class InvocationData:
    baseline: pd.DataFrame
    test: Optional[pd.DataFrame]
    combined: pd.DataFrame


def _conform(data: pd.DataFrame, revision_name: str, invocation: Invocation) -> pd.DataFrame:
    data = data.copy()
    data['sequence_num'] = range(1, len(data) + 1)
    data['revision'] = revision_name
    data['time'] = invocation.setup_duration_median + data['duration_mean'].cumsum()
    data['heap_bytes'] = invocation.setup_net_bytes + data['net_bytes'].cumsum()
    return data


def load_invocation_data(baseline: Tuple[str, Invocation], test: Optional[Tuple[str, Invocation]] = None) -> InvocationData:
    baseline_revision_name, baseline_invocation = baseline
    baseline_data = _conform(load_commands(baseline_invocation.id), baseline_revision_name, baseline_invocation)
    combined = baseline_data
    
    test_data: Optional[pd.DataFrame] = None
    if test is not None:
        test_revision_name, test_invocation = test
        test_data = _conform(load_commands(test_invocation.id), test_revision_name, test_invocation)
        combined = pd.concat([baseline_data, test_data])

    return InvocationData(baseline_data, test_data, combined)


def _color_map(conformed: pd.DataFrame) -> Dict[str, str]:
    revisions = conformed['revision'].unique()
    color_map = {}
    if len(revisions) >= 1:
        color_map[revisions[0]] = '#5d00e8'
    if len(revisions) >= 2:
        color_map[revisions[1]] = '#36bdea'
    return color_map


def plot_command_durations(data: InvocationData):
    fig = px.line(
        data.combined,
        x='sequence_num',
        y='duration_mean',
        markers=True,
        color='revision',
        color_discrete_map=_color_map(data.combined),
        hover_data=['label', 'time', 'command_id'],
        title='Main-thread CPU time per command',
        labels={
            'sequence_num': 'Command Execution',
            'duration_mean': 'Mean Duration (ns)',
            'duration_median': 'Median Duration (ns)',
        }
    )

    # Add confidence interval shaded regions
    color_map = _color_map(data.combined)
    revisions = data.combined['revision'].unique()

    for revision in revisions:
        revision_data = data.combined[data.combined['revision'] == revision].copy()
        revision_data = revision_data.sort_values('sequence_num')

        # Get the color for this revision and convert to rgba with transparency
        base_color = color_map.get(revision, '#888888')
        # Convert hex to rgba (e.g., #5d00e8 -> rgba(93, 0, 232, 0.2))
        if base_color.startswith('#'):
            r = int(base_color[1:3], 16)
            g = int(base_color[3:5], 16)
            b = int(base_color[5:7], 16)
            fill_color = f'rgba({r}, {g}, {b}, 0.2)'
        else:
            fill_color = base_color

        # Add the confidence interval as a filled area
        fig.add_trace(go.Scatter(
            x=revision_data['sequence_num'].tolist() + revision_data['sequence_num'].tolist()[::-1],
            y=revision_data['duration_ci95_hi'].tolist() + revision_data['duration_ci95_lo'].tolist()[::-1],
            fill='toself',
            fillcolor=fill_color,
            mode='lines',
            line=dict(width=0),
            showlegend=False,
            hoverinfo='skip',
            name=f'{revision} CI'
        ))

    # Move the line traces to the front
    fig.data = fig.data[-len(revisions):] + fig.data[:-len(revisions)]

    fig.update_layout(showlegend=False)
    return fig


def plot_command_allocations(data: InvocationData):
    fig = px.bar(
        data.combined,
        x='sequence_num',
        y='num_allocs',
        text_auto=True,
        barmode='group',
        color='revision',
        color_discrete_map=_color_map(data.combined),
        hover_data=['label', 'time', 'command_id'],
        title='Number of malloc() calls per command',
        labels={
            'sequence_num': 'Command Execution',
            'num_allocs': 'Number of allocations',
        }
    )
    fig.update_layout(showlegend=False)
    return fig


def plot_heap_timeline(data: InvocationData):
    fig = px.line(
        data.combined,
        x='sequence_num',
        y='heap_bytes',
        markers=True,
        color='revision',
        color_discrete_map=_color_map(data.combined),
        hover_data=['label', 'time', 'command_id'],
        title='Heap usage',
        labels={
            'sequence_num': 'Command Execution',
            'num_allocs': 'Number of allocations',
        }
    )
    fig.update_traces(fill='tozeroy')
    fig.update_layout(showlegend=False)
    return fig
