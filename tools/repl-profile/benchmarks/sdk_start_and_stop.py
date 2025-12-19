# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Initializes an SDK instance with logging as the only feature, then starts and stops the
core without performing any other API operations.
"""

INSTRUMENTED = """
set-config client-token fake-client-token
create-core
register-logging
create-logger
start-core
stop-core
"""
