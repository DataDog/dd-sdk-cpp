# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import sys
import struct
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
    create-core tracking-consent:granted
    register-crash-reporting
    start-core
    crash raise
    """)

    # When the repl crashes
    await p.join()
    assert p.exitcode != 0

    # Then the Crashpad database directory was initialized by StartHandler()
    crashes_dir = t.storage.get_artifact_dir('.crashes')
    assert (crashes_dir / 'settings.dat').exists(), \
        f'Crashpad database not initialized: {crashes_dir / "settings.dat"} not found'

    # And the Crashpad handler POSTed the minidump to the intake endpoint.
    # The handler uploads out-of-process, but join() drains the proxy after the process
    # exits, by which point the upload has already completed.
    assert len(p.requests) == 1, \
        f'Expected 1 request from Crashpad handler, got {len(p.requests)}'
    upload_request = p.requests[0]
    assert upload_request.method == 'POST'
    assert upload_request.url.path == '/crashpad-ingest-placeholder-path'
    # Header name lookup is case-insensitive: Crashpad sends 'Content-Type' (title case)
    content_type = next(
        (v for k, v in upload_request.headers.items() if k.lower() == 'content-type'), ''
    )
    assert content_type.startswith('multipart/form-data'), \
        f'Expected multipart/form-data, got: {content_type!r}'

    # And the Crashpad database contains exactly one minidump reflecting a completed
    # upload. Since the HTTP upload has completed by this point, the handler has
    # finished all its work and the database is in its final state.
    _assert_one_completed_minidump(crashes_dir)


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
        uint32  magic       (must equal 'CPAD' = 0x44415043)
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
    METADATA_MAGIC = 0x44415043  # 'CPAD' little-endian
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
