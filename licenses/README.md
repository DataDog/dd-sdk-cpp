# Third-party licenses

This directory contains the license text for each third-party component that can be
built into an installed build of the SDK. At install time, each file is copied to
`licenses/` only if its component is built into that configuration: see the install
rules in `CMakeLists.txt`, `cmake/crashpad.cmake`, and `cmake/deps/libcurl.cmake`.
`tools/check-package` verifies that each release package contains exactly the expected
set. `LICENSE-3rdparty.csv` lists all third-party components, including test-only ones.

Each file is either a verbatim copy of an upstream license file, or (where upstream
publishes no separate license file) the license comment from the top of a source file,
with comment markers removed. When updating a dependency, re-fetch its license text
from the source listed below at the new revision.

| File | Source |
|---|---|
| `crashpad.txt` | `LICENSE` from [crashpad](https://chromium.googlesource.com/crashpad/crashpad) at the revision pinned in `chromium/crashpad/.gclient_entries` |
| `mini_chromium.txt` | `LICENSE` from [mini_chromium](https://chromium.googlesource.com/chromium/mini_chromium) at the revision pinned in Crashpad's `DEPS` |
| `zlib.txt` | `LICENSE` from [Chromium's zlib](https://chromium.googlesource.com/chromium/src/third_party/zlib) at the revision pinned in Crashpad's `DEPS` |
| `getopt.txt` | `third_party/getopt/LICENSE` from crashpad |
| `lss.txt` | Lines 1-27 of `linux_syscall_support.h` from [lss](https://chromium.googlesource.com/linux-syscall-support) at the revision pinned in Crashpad's `DEPS` |
| `date.txt` | Lines 4-32 of `include/date/date.h` from [date](https://github.com/HowardHinnant/date) at the tag in `cmake/deps/date.cmake` |
| `curl.txt` | `COPYING` from [curl](https://github.com/curl/curl) at the release in `cmake/deps/libcurl.cmake` |
| `unicode-cldr.txt` | `LICENSE` from [CLDR](https://github.com/unicode-org/cldr), from the branch used by `tools/generate-windows-timezone-mapping` |
