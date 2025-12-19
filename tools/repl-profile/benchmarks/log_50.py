# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Makes 50 consecutive calls to Logger::Log().
"""

SETUP = """
set-config client-token fake-client-token
create-core
register-logging
create-logger
start-core
"""

INSTRUMENTED = """
log "hello world"
""" * 50

TEARDOWN = """
stop-core
"""
