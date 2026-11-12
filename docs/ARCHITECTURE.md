# Architecture & Design

Deox Download Manager (`deox-downloadmanager`) is built following the **Functional Core, Imperative Shell (FCIS)** pattern in modern C++23.

This architectural style ensures that all domain logic, chunk calculations, hashing, and state transitions are mathematically pure, deterministic, and easily unit-testable without mocking network sockets or filesystem side-effects.

---

## 🏛️ Layer Diagram

```
┌────────────────────────────────────────────────────────────────────────┐
│                              APPLICATIONS                              │
│   • apps/cli (deox-dm)           • apps/gui (deox-dm-gui / Qt 6)      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│                       deoxdm_engine (ORCHESTRATION)                    │
│   • DownloadEngine (worker threads, retries, EMA throughput)           │
│   • OutputFile (thread-safe positional disk writes)                    │
│   • CheckpointStore (crash-recovery serialization & resume)           │
│   • RateLimiter (token-bucket global bandwidth limiter)                │
└───────────────────┬───────────────────────────────┬────────────────────┘
                    │                               │
┌───────────────────▼─────────────┐   ┌─────────────▼────────────────────┐
│      deoxdm_net (NETWORK)       │   │   deoxdm_platform (ABSTRACTION)  │
│   • HttpClient (probe, ranges)  │   │   • File (pread/pwrite, prealloc)│
│   • CurlEasy (RAII libcurl)     │   │   • Terminal (TTY, ANSI, columns)│
└───────────────────┬─────────────┘   └─────────────┬────────────────────┘
                    │                               │
                    └───────────────┬───────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│                    deoxdm_core (FUNCTIONAL CORE)                       │
│   • Pure domain logic: zero side effects, zero I/O                     │
│   • Monadic error handling: std::expected<T, Error>                    │
│   • Chunk division & remainder balancing                              │
│   • URL parsing & path traversal defense                               │
│   • Deterministic state machine transitions                            │
│   • Built-in streaming SHA-256 (FIPS 180-4)                           │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 📦 Component Overview

### 1. `deoxdm_core` (Functional Core)
- **Referential Transparency**: Every function produces the same output for given inputs, without mutating global state or performing I/O.
- **Monadic Result Types**: Functions return `std::expected<T, Error>`. No C++ exceptions are thrown across domain boundaries.
- **Chunk Division (`chunk_planner.cpp`)**: Divides a file of size $N$ into $C$ connection slices. Remainder bytes are distributed deterministically so that:
  $$\sum_{i=0}^{C-1} \text{chunk}[i].\text{length}() = N$$
  Guarantees zero byte gaps and zero byte overlaps.
- **URL & Path Traversal Sanitization (`url.cpp`)**: Strict URI parsing, RFC 6266 `Content-Disposition` extraction, percent-decoding, and path injection defense against `../` or illegal filename characters.
- **Cryptographic Purity (`checksum.cpp`)**: Cleanroom, self-contained FIPS 180-4 SHA-256 implementation capable of streaming block updates.

### 2. `deoxdm_platform` (Platform Abstraction)
- Exposes pure C++23 interfaces (`platform::File`, `platform::Terminal`) with platform-specific backends.
- Encapsulates OS-specific system calls (`pwrite`/`pread`, `posix_fallocate`, `fdatasync`, `ioctl` on POSIX; `WriteFile` with `OVERLAPPED`, `SetFileInformationByHandle`, `MoveFileExW` on Windows).

### 3. `deoxdm_net` (Network Layer)
- Lightweight wrapper around `libcurl` runtime C API.
- Implements capability probe (validating HTTP 206 Partial Content, Accept-Ranges, Content-Length, ETags, and Last-Modified headers).
- Verifies range boundaries on every response slice to prevent silent server corruption.

### 4. `deoxdm_engine` (Imperative Orchestration)
- Spawns parallel worker threads utilizing `std::jthread` and cooperative `std::stop_token` cancellation.
- Tracks smoothed throughput using Exponential Moving Average (EMA) ($\alpha = 0.2$) for steady speed and accurate ETA forecasting.
- Enforces shared token-bucket rate limiting across all concurrent connections.
- Persists progress checkpoints (`.deoxdm`) atomically using temporary file staging and atomic renames.

### 5. `apps/cli` & `apps/gui` (User Frontends)
- **`deox-dm`**: Command-line application with multi-line ANSI terminal dashboard and automated non-TTY plain log fallback for CI/CD pipes.
- **`deox-dm-gui`**: Desktop graphical interface built with Qt 6 Widgets, offering a download queue, per-task progress bars, bandwidth throttling, and system desktop integration.
