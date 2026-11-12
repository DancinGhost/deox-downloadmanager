# Platform Support & Portability Guide

Deox Download Manager is engineered from first principles with a modular, cross-platform architecture. While **Linux (x86_64, aarch64)** is currently our primary, Tier-1 supported and CI-verified production target, the codebase is strictly decoupled from POSIX-specific assumptions to support cross-platform portability.

---

## 🏗️ Architectural Decoupling

Operating system interactions are encapsulated behind explicit, abstract interfaces located under:
- `include/deoxdm/platform/file.hpp`: Thread-safe positional I/O, atomic file commits, sparse allocation, and preallocation.
- `include/deoxdm/platform/terminal.hpp`: Console TTY detection, column calculation, and ANSI VT escape sequence handling.

Upper architectural layers (`deoxdm_core`, `deoxdm_net`, `deoxdm_engine`, and `apps`) depend strictly on these platform header abstractions, reporting native errors via standard `std::error_code` within `std::system_category()`.

```
 apps/cli & apps/gui
         │
  deoxdm_engine ────► deoxdm_net (libcurl)
         │
  deoxdm_platform (Interface)
   ├── src/platform/posix/   (Linux, macOS, *BSD)
   └── src/platform/windows/ (Win32 API)
         │
   deoxdm_core (100% Pure, Zero Dependencies)
```

---

## 📊 Platform Support Matrix

| Platform | Tier | File I/O Mechanism | Preallocation | Atomic Checkpoints | CI Tested |
| :--- | :---: | :--- | :--- | :--- | :---: |
| **Linux** (glibc / musl) | **Tier 1** | Concurrent `pwrite(2)` / `pread(2)` | `posix_fallocate(2)` | Atomic `rename(2)` | ✅ Yes (GCC & Clang) |
| **macOS** (Darwin) | Tier 2 | Concurrent `pwrite(2)` / `pread(2)` | `fcntl(F_PREALLOCATE)` | `rename(2)` + `F_FULLFSYNC` | 🔄 Planned |
| **FreeBSD / OpenBSD** | Tier 2 | Concurrent `pwrite(2)` / `pread(2)` | `posix_fallocate(2)` | Atomic `rename(2)` | 🔄 Planned |
| **Microsoft Windows** | Tier 2 | Win32 `OVERLAPPED` `WriteFile` | `SetFileInformationByHandle` | `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` | 🔬 Experimental |

---

## 🐧 Tier 1: Linux Details

- **Positional Thread-Safe Disk I/O**: Multiple connection workers write downloaded slices directly to disk at designated byte offsets using `pwrite(2)`. This bypasses thread contention and eliminates seeking locks.
- **Disk Preallocation**: Uses `posix_fallocate(2)` to reserve contiguous disk blocks upfront. If the underlying filesystem (e.g. some network mounts or FAT32) does not support fallocate, it transparently falls back to sparse sizing via `ftruncate(2)`.
- **Atomic Crash Recovery**: Checkpoints (`.deoxdm`) are flushed to disk via `fdatasync(2)` in a temporary file and atomically committed with `rename(2)`. Sudden power loss or crashes will never corrupt state.
- **ANSI Terminal Driver**: Uses `ioctl(TIOCGWINSZ)` to dynamically query console dimensions, respecting `NO_COLOR` and `TERM=dumb` conventions.

---

## 🪟 Windows Implementation (Experimental)

A clean Win32 implementation is provided in `src/platform/windows/`:
- **Thread-Safe Positional I/O**: Leverages `WriteFile` and `ReadFile` with an `OVERLAPPED` structure (`Offset` and `OffsetHigh`), enabling concurrent positional writes without requiring custom locking primitives.
- **Preallocation**: Uses `FILE_ALLOCATION_INFO` via `SetFileInformationByHandle` to allocate disk clusters without seeking, checking for `ERROR_DISK_FULL` and `ERROR_DISK_QUOTA_EXCEEDED`.
- **Atomic Renaming**: Uses `MoveFileExW` with flags `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`.
- **Virtual Terminal Mode**: Automatically activates `ENABLE_VIRTUAL_TERMINAL_PROCESSING` via `SetConsoleMode` so multi-color progress bars render properly on modern Windows Terminal and ConHost.

---

## 🛠️ Building for Non-Linux Platforms

To target other platforms using CMake:
```bash
# CMake automatically selects the platform backend based on the target OS:
# WIN32 -> src/platform/windows/
# UNIX  -> src/platform/posix/
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```
