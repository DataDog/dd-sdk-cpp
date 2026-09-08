# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import re
import sys
import struct
import json
import time
import uuid
import email
import email.policy
from pathlib import Path
from lib.test import TestContext

# This test only runs when the SDK was compiled with DD_CRASH_MODE=crashpad
CRASH_MODE = 'crashpad'


async def main(t: TestContext):
    """
    CrashReporting (Crashpad): smoke test
    """
    # Given a repl process with crash reporting registered and the core started
    p = t.spawn_repl()
    p.run("""
    set-config client-token fake-client-token
    set-config rum-application-id a991ca10-4004-4004-4004-beefbeefbeef
    create-core tracking-consent:granted
    register-crash-reporting
    register-rum
    start-core
    set-user-info usr-123 name:"Alice" email:"alice@example.com"
    add-user-extra-info attr:plan:premium
    set-account-info acct-456 name:"Acme"
    add-account-extra-info attr:tier:gold
    add-rum-attribute attr:foo:hello attr:bar:world
    start-view crash-view name:"Crash View" attr:bar:alpha attr:baz:bravo
    sleep 10
    crash raise
    """)

    # When the repl crashes
    await p.join()
    crash_time_ms = time.time_ns() // 1_000_000
    assert p.exitcode != 0

    # Then the Crashpad database directory was initialized by StartHandler()
    crashes_dir = t.storage.get_artifact_dir('.crashes')
    assert (crashes_dir / 'settings.dat').exists(), \
        f'Crashpad database not initialized: {crashes_dir / "settings.dat"} not found'

    # And the Crashpad handler POSTed the minidump to the intake endpoint (the handler
    # uploads out-of-process, but join() drains the proxy after the process exits, by
    # which point the upload has already completed)
    assert len(p.requests) == 1, \
        f'Expected 1 request from Crashpad handler, got {len(p.requests)}'
    upload_request = p.requests[0]
    assert upload_request.method == 'POST'
    assert upload_request.url.path == '/api/v2/minidump'

    # And the request carries our RUM Application's client token in its DD-API-KEY
    # header
    assert upload_request.headers['DD-API-KEY'] == 'fake-client-token'

    # And the DD-EVP-ORIGIN and DD-EVP-ORIGIN-VERSION headers identify the SDK that was
    # responsible for this crash report
    assert upload_request.headers['DD-EVP-ORIGIN'] == 'cpp'
    sdk_version = upload_request.headers['DD-EVP-ORIGIN-VERSION']
    assert re.match(r'^\d+\.\d+\.\d+$', sdk_version), \
        f"Expected SemVer in DD-EVP-ORIGIN-VERSION; got {sdk_version}"

    # And DD-REQUEST-ID is set to a randomly-generated UUID
    assert uuid.UUID(upload_request.headers['DD-REQUEST-ID']) != uuid.UUID(int=0)

    # And that request has Content-Type: multipart/form-data (case-insensitive)
    content_type = next(
        (v for k, v in upload_request.headers.items() if k.lower() == 'content-type'), ''
    )
    assert content_type.startswith('multipart/form-data'), \
        f'Expected multipart/form-data, got: {content_type!r}'

    # And the form field values present in the HTTP request match the final set of
    # annotation values that are set by the handler: we have Crashpad's own 'guid', then
    # 'dd.rum.view' and 'dd.rum.error', and nothing else
    form_fields = _parse_multipart_form_fields(upload_request.body, content_type)
    assert 'guid' in form_fields, \
        f"Missing 'guid' value in form-data upload: {form_fields}"
    assert 'dd.rum.view' in form_fields, \
        f"Missing 'dd.rum.view' value in form-data upload: {form_fields}"
    assert 'dd.rum.error' in form_fields, \
        f"Missing 'dd.rum.error' value in form-data upload: {form_fields}"
    expected_keys = {
        'guid',
        'dd.rum.view',
        'dd.rum.error',
    }
    unexpected_form_field_keys = set(form_fields.keys()) - expected_keys
    assert not unexpected_form_field_keys, \
        f'Unexpected annotation values in form-data upload: {", ".join(unexpected_form_field_keys)}'

    # And both of those values are well-formed JSON objects
    view = json.loads(form_fields['dd.rum.view'])
    assert isinstance(view, dict)
    assert view['type'] == 'view'
    error = json.loads(form_fields['dd.rum.error'])
    assert isinstance(error, dict)
    assert error['type'] == 'error'

    # And the timestamp on the error event is roughly equivalent to the moment the
    # process exited, give or take a handful of seconds
    timestamp_error_ms = 5000
    assert abs(error['date'] - crash_time_ms) < timestamp_error_ms, \
        f"Expected dd.rum.error.date to be within {timestamp_error_ms}ms of {crash_time_ms}; got {error['date']}"

    # And the timestamp on the view event is recorded as 1ms prior to the time of the
    # crash
    assert view['date'] == error['date'] - 1, \
        f"Expected dd.rum.view.date to be 1ms less than dd.rum.error.date ({error['date']}); got {view['date']}"

    # And the error event carries the implicitly-configured service name
    assert error['service'] == 'dd-sdk-cpp-repl'
    assert 'service' not in view  # <-- For documentation. Is this a gap?

    # And both events reflect the same service and ddtags values for our repl test
    ddtags_regex = re.compile(r'^service:dd-sdk-cpp-repl,env:development,sdk_version:\d+\.\d+\.\d+$')
    assert ddtags_regex.match(view['ddtags'])
    assert ddtags_regex.match(error['ddtags'])

    # And both events have identical os and device properties
    _assert_identical_object_present_in_both('os', view, error)
    _assert_identical_object_present_in_both('device', view, error)

    # And both events have identical usr and account properties, both of which match the
    # details provided in the test
    _assert_usr_properties(view, error)
    _assert_account_properties(view, error)

    # And both events carry our configured application ID
    _assert_application_properties(view, error)

    # And both events belong to the same session, which the view event records as still
    # active at the time of the crash
    _assert_session_properties(view, error)

    # And the view event reflects the state of our application at the time of the crash,
    # while the error event is correlated with the same view
    _assert_view_properties(view, error)

    # And the error event records the basic details of the crash
    _assert_error_properties(error)

    # And both events carry the global RUM attributes that were set at the time of the
    # crash
    assert view['context']['foo'] == 'hello'
    assert error['context']['foo'] == 'hello'
    # Documented for clarity: there is a known gap in that we clobber all view-level
    # attributes and just use the global RUM attributes as encoded in crash context.
    # This is consistent with the current behavior of the iOS SDK, but we may want to
    # improve it eventually, in which case we'd expect 'context' to be:
    # - {"foo":"hello","bar":"alpha","baz":"bravo"}
    # See comments flagged TODO(RUM-15994).
    assert view['context']['bar'] == 'world'
    assert error['context']['bar'] == 'world'
    assert 'baz' not in view['context']
    assert 'baz' not in error['context']

    # And the Crashpad database contains exactly one minidump reflecting a completed
    # upload. Since the HTTP upload has completed by this point, the handler has
    # finished all its work and the database is in its final state.
    _assert_one_completed_minidump(crashes_dir)


def _parse_multipart_form_fields(body: bytes, content_type: str) -> dict:
    """
    Parses a multipart/form-data body and returns a dict mapping each form
    field name to its text value. Parts that carry a filename (i.e. file
    attachments such as the minidump) are skipped.

    Uses Python's stdlib `email` package to handle boundary extraction and
    MIME part parsing.
    """
    # Reconstruct a MIME message that the email parser can handle: prepend
    # the Content-Type header so the parser sees the boundary parameter.
    mime_bytes = (f'Content-Type: {content_type}\r\n\r\n').encode() + body
    msg = email.message_from_bytes(mime_bytes, policy=email.policy.compat32)

    fields = {}
    for part in msg.get_payload():
        disposition = part.get('Content-Disposition', '')
        # Skip file-attachment parts (those with a filename parameter)
        if 'filename' in disposition:
            continue
        # Extract the field name from Content-Disposition
        _, params = email.header.decode_header(disposition)[0][0], {}
        # Use get_param for reliable parameter extraction
        name = part.get_param('name', header='Content-Disposition')
        if name is None:
            continue
        payload = part.get_payload(decode=False)
        if isinstance(payload, bytes):
            payload = payload.decode('utf-8', errors='replace')
        fields[name] = payload.strip() if payload else ''
    return fields


def _assert_object_present_in_both(property_name: str, view: dict, error: dict) -> tuple[dict, dict]:
    assert property_name in view, \
        f"'dd.rum.view' is missing expected property '{property_name}'"
    view_obj = view[property_name]
    assert isinstance(view_obj, dict), \
        f"'dd.rum.view.{property_name}' is not an object (got {view_obj})"

    assert property_name in error, \
        f"'dd.rum.error' is missing expected property '{property_name}'"
    error_obj = error[property_name]
    assert isinstance(error_obj, dict), \
        f"'dd.rum.error.{property_name}' is not an object (got {error_obj})"

    return view_obj, error_obj


def _assert_identical_object_present_in_both(property_name: str, view: dict, error: dict) -> dict:
    view_obj, error_obj = _assert_object_present_in_both(property_name, view, error)
    if view_obj != error_obj:
        f"Mismatch in 'dd.rum.view.{property_name}' vs 'dd.rum.error.{property_name}': got {view_obj} vs. {error_obj}"
    return view_obj


def _assert_usr_properties(view: dict, error: dict):
    usr = _assert_identical_object_present_in_both('usr', view, error)
    assert usr['id'] == 'usr-123'
    assert usr['name'] == 'Alice'
    assert usr['email'] == 'alice@example.com'
    assert uuid.UUID(usr['anonymous_id']) != uuid.UUID(int=0)
    assert usr['plan'] == 'premium'


def _assert_account_properties(view: dict, error: dict):
    account = _assert_identical_object_present_in_both('account', view, error)
    assert account['id'] == 'acct-456'
    assert account['name'] == 'Acme'
    assert account['tier'] == 'gold'


def _assert_application_properties(view: dict, error: dict):
    application = _assert_identical_object_present_in_both('application', view, error)
    assert application['id'] == 'a991ca10-4004-4004-4004-beefbeefbeef'


def _assert_session_properties(view: dict, error: dict):
    view_session, error_session = _assert_object_present_in_both('session', view, error)
    assert view_session['id'] == error_session['id']
    assert view_session['type'] == error_session['type'] == 'user'
    assert view_session['is_active'] == True

    # Expected: session.is_active is only set on view events
    assert 'is_active' not in error_session


def _assert_view_properties(view: dict, error: dict):
    # Both events have a top-level 'view' object
    view_view, error_view = _assert_object_present_in_both('view', view, error)

    # The commom subset of view properties is identical between view and error events:
    # i.e. the error was recorded in the context of the view
    assert view_view['id'] == error_view['id']
    assert view_view['url'] == error_view['url']
    assert view_view['name'] == error_view['name']

    # All view properties on the view event match the application state from our test
    v = view_view
    assert v['url'] == 'crash-view'
    assert v['name'] == 'Crash View'
    assert v['is_active'] == False
    assert v['error']['count'] == 1
    assert v['crash']['count'] == 1


def _assert_error_properties(error: dict):
    # error is a top-level object on the error event
    assert 'error' in error, \
        f"'dd.rum.error' is missing expected property 'error'"
    e = error['error']
    assert isinstance(e, dict), \
        f"'dd.rum.error.error' is not an object (got {e})"

    # error.id is a valid UUID; error.source is set to 'source' (i.e. the fault was in
    # runtime execution of the code, indicating a bug in the application source), and
    # error.is_crash is true
    assert uuid.UUID(e['id']) != uuid.UUID(int=0)
    assert e['source'] == 'source'
    assert e['is_crash'] == True

    # error.source_type indicates the platform
    if sys.platform == 'darwin':
        assert e['source_type'] == 'macos'
    elif sys.platform == 'win32':
        assert e['source_type'] == 'windows'
    else:
        assert e['source_type'] == 'linux'

    # error.message reflects the exception code detected by Crashpad
    want_message = 'Application crash: SIGSEGV (Segmentation fault)'
    if sys.platform == 'win32':
        want_message = 'Application crash: EXCEPTION_ACCESS_VIOLATION (0xC0000005)'
    assert e['message'] == want_message, \
        f"Unexpected value for 'dd.rum.error.message': {e['message']}"

    # No error.stack or error.binary_images values are present; the backend is
    # responsible for pulling this data from the uploaded .dmp file
    assert 'stack' not in e
    assert 'binary_images' not in e


def _assert_one_completed_minidump(crashes_dir: Path):
    """
    Asserts that the Crashpad database at `crashes_dir` contains exactly one minidump
    and that it reflects a completed upload.

    The database layout differs by platform:
    - macOS/Linux: reports move between new/, pending/, and completed/ subdirectories
      as state advances; a completed report's .dmp file lives in completed/.
    - Windows: all .dmp files live in a flat reports/ directory, with state tracked
      in a binary `metadata` file. See crash_report_database_win.cc for the format.
    """
    if sys.platform == 'win32':
        _assert_one_completed_minidump_windows(crashes_dir)
    else:
        completed_dir = crashes_dir / 'completed'
        dmp_files = list(completed_dir.glob('*.dmp'))
        assert len(dmp_files) == 1, \
            f'Expected 1 .dmp in {completed_dir}, found {len(dmp_files)}: {dmp_files}'


def _assert_one_completed_minidump_windows(crashes_dir: Path):
    """
    On Windows, validates that reports/ contains exactly one .dmp and that the
    corresponding record in the `metadata` file has state=kCompleted and
    attributes&kAttributeUploaded set.

    Binary format (little-endian) defined in crash_report_database_win.cc:

      MetadataFileHeader (16 bytes):
        uint32  magic       (must equal 'CPAD' = 0x43504144, MSVC multichar literal)
        uint32  version     (must equal 1)
        uint32  num_records
        uint32  padding

      MetadataFileReportRecord (56 bytes each), repeated num_records times:
        uint8[16]  uuid
        uint32     file_path_index   (offset into string table)
        uint32     id_index          (offset into string table)
        int64      creation_time
        int64      last_upload_attempt_time
        int32      upload_attempts
        int32      state             (0=kPending, 1=kUploading, 2=kCompleted)
        uint8      attributes        (bit 0 = kAttributeUploaded)
        uint8[7]   padding

      String table: null-terminated UTF-8 strings, indexed by byte offset.
    """
    reports_dir = crashes_dir / 'reports'
    dmp_files = list(reports_dir.glob('*.dmp'))
    assert len(dmp_files) == 1, \
        f'Expected 1 .dmp in {reports_dir}, found {len(dmp_files)}: {dmp_files}'

    metadata_path = crashes_dir / 'metadata'
    data = metadata_path.read_bytes()

    # Parse the header
    HEADER_SIZE = 16
    METADATA_MAGIC = 0x43504144  # 'CPAD' as MSVC multichar literal: 'C'<<24|'P'<<16|'A'<<8|'D'
    METADATA_VERSION = 1
    K_COMPLETED = 2
    K_ATTRIBUTE_UPLOADED = 1 << 0

    magic, version, num_records, _ = struct.unpack_from('<IIII', data, 0)
    assert magic == METADATA_MAGIC, f'Unexpected metadata magic: 0x{magic:08X}'
    assert version == METADATA_VERSION, f'Unexpected metadata version: {version}'
    assert num_records == 1, \
        f'Expected 1 record in metadata, found {num_records}'

    # Parse the single report record
    offset = HEADER_SIZE
    # uuid (16) + file_path_index (4) + id_index (4) + creation_time (8) +
    # last_upload_attempt_time (8) + upload_attempts (4) + state (4) +
    # attributes (1) + padding (7) = 56 bytes
    _uuid = data[offset:offset + 16]
    state, attributes = struct.unpack_from('<iB', data, offset + 44)

    assert state == K_COMPLETED, \
        f'Expected report state kCompleted (2), got {state}'
    assert attributes & K_ATTRIBUTE_UPLOADED, \
        f'Expected kAttributeUploaded bit set in attributes, got 0x{attributes:02X}'
