# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.

from lib.test import TestContext


async def main(t: TestContext):
    """
    RUM: smoke test

    Performs a basic smoke test to verify that we can register the RUM feature and
    generate view, action, resource, and error events, with custom attributes applied at
    the global, view, and per-event levels.
    """
    p = t.spawn_repl()
    p.run("""
        set-config client-token fake-client-token
        set-config rum-application-id a991ca10-4004-4004-4004-beefbeefbeef
        set-config flush-on-stop
        create-core tracking-consent:granted
        register-rum
        start-core
    """)

    # Set a global RUM attribute — should appear on all events
    p.run("""
        add-rum-attribute attr:env:test
    """)

    # Start a view with a per-StartView attribute, then add a view attribute
    p.run("""
        start-view home name:"Home" attr:section:main
        add-view-attribute attr:variant:b
    """)

    # Add an action with a per-event attribute
    p.run("""
        add-action "tap button" type:tap attr:button-id:submit
    """)

    # Start and stop a resource with attributes on both calls
    p.run("""
        start-resource res1 https://example.com/api method:get attr:req-tag:r1
        stop-resource res1 status:200 size:1234 type:fetch attr:cache:hit
    """)

    # Add an error with a per-event attribute
    p.run("""
        add-error "something broke" source:custom attr:code:404
    """)

    p.run("""
        stop-view home
        stop-core
        exit
    """)
    await p.join()

    assert p.exitcode == 0, f'repl exited with code {p.exitcode}\n{p.stderr}'

    rum_events = [e for req in p.requests if req.url.path == '/api/v2/rum' for e in req.json]
    assert len(rum_events) >= 1, f'Expected at least 1 RUM event, got {len(rum_events)}'

    views = [e for e in rum_events if e['type'] == 'view']
    actions = [e for e in rum_events if e['type'] == 'action']
    resources = [e for e in rum_events if e['type'] == 'resource']
    errors = [e for e in rum_events if e['type'] == 'error']

    assert len(views) >= 1, f'Expected at least 1 view event, got: {views}'
    assert len(actions) == 1, f'Expected 1 action event, got: {actions}'
    assert len(resources) == 1, f'Expected 1 resource event, got: {resources}'
    assert len(errors) == 1, f'Expected 1 error event, got: {errors}'

    # All events should carry the global RUM attribute
    for event in rum_events:
        ctx = event.get('context', {})
        assert ctx.get('env') == 'test', \
            f'Expected context.env=test on {event["type"]} event, got: {event}'

    # All view events should carry the StartView attr
    for view in views:
        ctx = view.get('context', {})
        assert ctx.get('section') == 'main', \
            f'Expected context.section=main on view event, got: {view}'

    # The final (stopped) view event should also carry the add-view-attribute attr
    final_view = next(v for v in reversed(views) if not v['view']['is_active'])
    assert final_view.get('context', {}).get('variant') == 'b', \
        f'Expected context.variant=b on final view event, got: {final_view}'

    # Action should carry its per-event attribute
    action_ctx = actions[0].get('context', {})
    assert action_ctx.get('button-id') == 'submit', \
        f'Expected context.button-id=submit on action, got: {actions[0]}'

    # Resource should carry attributes from both StartResource and StopResource
    resource_ctx = resources[0].get('context', {})
    assert resource_ctx.get('req-tag') == 'r1', \
        f'Expected context.req-tag=r1 on resource, got: {resources[0]}'
    assert resource_ctx.get('cache') == 'hit', \
        f'Expected context.cache=hit on resource, got: {resources[0]}'

    # Error should carry its per-event attribute
    error_ctx = errors[0].get('context', {})
    assert error_ctx.get('code') == '404', \
        f'Expected context.code=404 on error, got: {errors[0]}'
