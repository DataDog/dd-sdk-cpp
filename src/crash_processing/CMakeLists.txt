# crash_processing: self-contained logic for producing RUM events from crash reports,
# used by the rum module as well as the Crashpad handler process (see
# handlers/crashpad/datadog.patch in crash_reporting). It depends only on types, not on
# core, since the handler process doesn't run the SDK's core.
dd_add_module(crash_processing
    DEPENDS types
    SOURCES
        datadog/impl/crash_processing/crash_context_annotations.cpp
        datadog/impl/crash_processing/crash_events.cpp
        datadog/impl/crash_processing/crash_formatting.cpp
        datadog/impl/crash_processing/view_event_mutation.cpp
        datadog/impl/crash_processing/view_event_parser.cpp
)
