# Platform Support & Porting Guide

Linux is the supported platform: it is the only one built and tested in CI.
The code base is organized so that macOS, the BSDs and Windows can be added
by writing backend code in one place, without touching the engine or the
applications.

## Support matrix

| Platform | Status | Backend | Built in CI |
| :--- | :--- | :--- | :---: |
| **Linux** (glibc, musl) | Supported | `src/platform/posix/` | ✅ GCC 14, Clang 19 |
| **macOS** | Planned | `src/platform/posix/` (has macOS-specific paths) | ❌ |
| **FreeBSD / OpenBSD** | Planned | `src/platform/posix/` | ❌ |
| **Windows** | Planned, backend drafted | `src/platform/windows/` | ❌ never compiled |

## Where operating-system code may live

| Layer | OS headers allowed? | Notes |
| :--- | :--- | :--- |
| `src/core/` | No | Standard library only. Builds unchanged everywhere. |
| `src/platform/` | **Yes: only here** | The single place for `<unistd.h>`, `<windows.h>`, signals, consoles. |
| `src/net/` | No | libcurl only, through `curl_abi.hpp`. |
| `src/engine/` | No | Uses `deoxdm/platform/*.hpp` for files, paths. |
| `apps/cli/` | No | Uses `deoxdm/platform/*.hpp` for the terminal and Ctrl-C. |
| `apps/gui/` | No | Qt provides the platform services (paths, opening files, dialogs). |
| `tests/` | Avoid | Exception: `tests/support/mock_http_server.hpp` uses BSD sockets. |

To check this rule, search for OS headers outside `src/platform/`:

```bash
grep -rnE '#include <(unistd|fcntl|sys/|windows|signal|termios)' src apps | grep -v src/platform
```

## The platform layer

Public interface: `include/deoxdm/platform/`

| Header | Provides |
| :--- | :--- |
| `file.hpp` | `File` with positional (thread-safe) reads and writes, preallocation, `sync()`; `write_file_atomically()`; `is_out_of_space()` |
| `path.hpp` | `path_from_utf8()` / `path_to_utf8()`: the only correct way to turn UTF-8 text into a `std::filesystem::path` and back on Windows |
| `terminal.hpp` | TTY detection, terminal width, ANSI escape enablement, `supports_color()` (honours `NO_COLOR`, `TERM=dumb`) |
| `interrupt.hpp` | `install_interrupt_handler()` / `interrupt_requested()`: Ctrl-C and termination requests |

Implementation: `src/platform/`

```
src/platform/
├── backend.hpp      internal: primitives every backend must provide
├── common/          OS-independent code, built on every platform
│   ├── file.cpp         atomic_temp_path, write_file_atomically
│   ├── interrupt.cpp    the shared "stop requested" flag
│   ├── path.cpp         UTF-8 <-> path conversion
│   └── terminal.cpp     supports_color
├── posix/           Linux, macOS, BSDs
│   ├── file.cpp         open/pread/pwrite, posix_fallocate, fdatasync, rename
│   ├── interrupt.cpp    sigaction(SIGINT, SIGTERM, SIGHUP)
│   └── terminal.cpp     isatty, ioctl(TIOCGWINSZ)
└── windows/         Win32
    ├── file.cpp         CreateFileW, ReadFile/WriteFile with OVERLAPPED, MoveFileExW
    ├── interrupt.cpp    SetConsoleCtrlHandler
    └── terminal.cpp     GetConsoleMode, ENABLE_VIRTUAL_TERMINAL_PROCESSING
```

`cmake/DeoxdmPlatform.cmake` picks the backend (`WIN32` → `windows`,
`UNIX` → `posix`); `src/platform/CMakeLists.txt` compiles `common/` plus that
backend.

### Text and paths

All text inside the program is UTF-8. A `std::filesystem::path` built from a
`std::string` is **not** read as UTF-8 on Windows (the ANSI code page is used),
and `path::string()` can throw there. Always use `platform::path_from_utf8()`
and `platform::path_to_utf8()` when a path meets text: file names from servers,
error messages, checkpoint files, `QString`. On POSIX they are plain copies.

## Adding a backend

1. Create `src/platform/<name>/` with `file.cpp`, `interrupt.cpp` and
   `terminal.cpp`, implementing:
   - everything declared in `include/deoxdm/platform/` that `common/` does not
     define, and
   - the primitives in `src/platform/backend.hpp` (`remove_file_quietly`,
     `replace_file`, and setting `detail::g_interrupt_requested`).
2. Select it in `cmake/DeoxdmPlatform.cmake`.
3. Run `deoxdm_tests`. `tests/unit/test_platform.cpp` checks the contracts
   every backend must meet (positional I/O, atomic writes, UTF-8 file names,
   interrupts).

## Known gaps per platform

### macOS / BSDs

The POSIX backend already contains the macOS-specific calls
(`F_FULLFSYNC` instead of `fdatasync`, `F_PREALLOCATE` instead of
`posix_fallocate`). None of it has been compiled or run yet. Still to do:

- A CI job (`macos-14` runner; `brew install qt curl`).
- An `Info.plist` and `.icns` icon for the `deox-dm-gui.app` bundle
  (`packaging/macos/`).

### Windows

`src/platform/windows/` is a draft that has never been compiled. Still to do:

- **Compile and test** the backend (MSVC and MinGW-w64), then add a CI job.
- **UTF-8 command line and console.** `argv` arrives in the ANSI code page.
  Embed an application manifest with `<activeCodePage>UTF-8</activeCodePage>`
  (Windows 10 1903+), or use `wmain`, and call `SetConsoleOutputCP(CP_UTF8)`
  so non-ASCII file names print correctly.
- **Integration tests.** `tests/support/mock_http_server.hpp` uses BSD sockets
  and needs a small Winsock shim (`WSAStartup`, `closesocket`, `SOCKET`).
  Until then `tests/CMakeLists.txt` builds the integration and GUI tests on
  UNIX only.
- **libcurl.** `src/net/curl_abi.hpp` declares the libcurl functions it uses
  instead of including `curl/curl.h`. The declarations match the Windows x64
  ABI, but linking needs the import library (`libcurl.lib`, e.g. from vcpkg).
- **Closing the console window.** Windows ends the process about two seconds
  after the request, so a checkpoint may be up to one save interval old.
- **Packaging.** An installer (`packaging/windows/`), e.g. with WiX or CPack
  NSIS, and deployment of the Qt DLLs with `windeployqt`.
