# logging: the Logging feature, which sends log events to Datadog.
dd_add_module(logging
    DEPENDS core
    PUBLIC_HEADERS logging
    SOURCES
        datadog/c/logging.cpp
        datadog/cpp/logging.cpp
        datadog/impl/logging/event_generation.cpp
        datadog/impl/logging/logger.cpp
        datadog/impl/logging/logging.cpp
        datadog/impl/logging/tags.cpp
)
