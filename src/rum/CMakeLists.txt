# rum: the RUM feature, which tracks sessions, views, actions, resources, and errors,
# and turns processed crash reports into RUM error events.
dd_add_module(rum
    DEPENDS core crash_processing
    PUBLIC_HEADERS rum
    SOURCES
        datadog/c/rum.cpp
        datadog/cpp/rum.cpp
        datadog/impl/rum/containers/resource_map.cpp
        datadog/impl/rum/containers/view_array.cpp
        datadog/impl/rum/scopes/action.cpp
        datadog/impl/rum/scopes/application.cpp
        datadog/impl/rum/scopes/resource.cpp
        datadog/impl/rum/scopes/session.cpp
        datadog/impl/rum/scopes/view.cpp
        datadog/impl/rum/context.cpp
        datadog/impl/rum/rum.cpp
        datadog/impl/rum/scope.cpp
)
