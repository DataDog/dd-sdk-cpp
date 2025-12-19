# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Helpers for using the Datadog API to upload and describe metrics.
"""
import sys
import time
from typing import List, Tuple

from lib.metric import PerfMetricValue, PerfMetricFunc


def upload_metrics(metric_values: List[Tuple[str, PerfMetricValue]], tags: List[str]):
    # Import from datadog-api-client at function scope so the run_benchmark.py script
    # will still be usable without any third-party dependencies
    try:
        from datadog_api_client.v2 import ApiClient, Configuration
        from datadog_api_client.v2.api.metrics_api import MetricsApi
        from datadog_api_client.v2.model.metric_intake_type import MetricIntakeType
        from datadog_api_client.v2.model.metric_point import MetricPoint
        from datadog_api_client.v2.model.metric_series import MetricSeries
        from datadog_api_client.v2.model.metric_payload import MetricPayload
    except ImportError:
        print('ERROR: Unable to upload metrics: datadog-api-client is not installed!')
        sys.exit(1)

    assert metric_values    
    timestamp = int(time.time())

    configuration = Configuration()
    with ApiClient(configuration) as api_client:
        metrics_api = MetricsApi(api_client)

        series: List[MetricSeries] = []
        for name, value in metric_values:
            series.append(MetricSeries(
                metric=name,
                type=MetricIntakeType.GAUGE,
                points=[MetricPoint(timestamp=timestamp, value=value)],
                tags=tags,
            ))

        body = MetricPayload(series=series)
        print(body)
        response = metrics_api.submit_metrics(body=body)
        print(response)


def update_metrics_metadata(metrics: List[PerfMetricFunc]):
    try:
        from datadog_api_client.v1 import ApiClient, Configuration
        from datadog_api_client.v1.api.metrics_api import MetricsApi
        from datadog_api_client.v1.model.metric_metadata import MetricMetadata
    except ImportError:
        print('ERROR: Unable to update metrics metadata: datadog-api-client is not installed!')
        sys.exit(1)

    assert metrics

    configuration = Configuration()
    with ApiClient(configuration) as api_client:
        metrics_api = MetricsApi(api_client)

        for metric in metrics:
            if not metric.description:
                continue
            body = MetricMetadata(
                description=metric.description,
            )
            response = metrics_api.update_metric_metadata(metric_name=metric.metric_name, body=body)
            print(response)
