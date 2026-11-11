# Deox Download Manager (`deox`)

[![CI](https://github.com/DancinGhost/deox-downloadmanager/actions/workflows/ci.yml/badge.svg)](https://github.com/DancinGhost/deox-downloadmanager/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/C%2B%2B-23-orange.svg)](https://en.wikipedia.org/wiki/C%2B%2B23)
[![Branch](https://img.shields.io/badge/branch-main-success.svg)](https://github.com/DancinGhost/deox-downloadmanager/tree/main)

> **Deox** is a blazingly fast, memory-safe, segmented download manager written in modern C++23 using the **Functional Core, Imperative Shell (FCIS)** architecture. No hidden runtime mechanisms, no garbage collection, and zero viral GPL licensing issues.

<p align="center">
  <img src="assets/terminal-preview.svg" alt="Deox CLI Terminal Demo" width="850">
</p>

---

## 🎯 Motivation: C++ vs. Rust

In recent years, the systems programming community has witnessed a surge of CLI tools written in Rust, driven by the narrative that C++ cannot provide memory safety without sacrificing developer ergonomics.

**Deox proves that modern C++23 can be just as safe, explicit, and fast as Rust.**

| Feature | Rust Download Managers | Deox (Modern C++23) |
| :--- | :--- | :--- |
| **Safety Invariants** | Ownership borrow checker | Value semantics, RAII, bounds-checked `std::span` |
| **Error Handling** | `Result<T, E>` monadic types | `std::expected<T, E>` monadic error handling |
| **Concurrency Model** | `tokio` async runtime (heavy state machine) | Native OS `std::jthread`, cooperative cancellation (`std::stop_token`) |
| **File I/O Throughput** | Multi-threaded file writes | Lock-free concurrent positional writes (`pwrite(2)`) |
| **Disk Allocation** | Varies by implementation | Zero-fragmentation preallocation (`posix_fallocate(2)`) |
| **Hidden Mechanisms** | Hidden async poll loops, large runtime crates | **Zero hidden mechanisms**: completely explicit, deterministic |
| **Licensing** | Varies (some depend on GPL crates) | **100% Permissive MIT License** (cleanroom, zero GPLv3 contamination) |

---

## 🏛️ Architecture: Functional Core, Imperative Shell

Deox strictly separates pure business logic from side-effectful I/O.

```
                    ┌───────────────────────────────────────────────┐
                    │               IMPERATIVE SHELL                │
                    │  (Network I/O, Positional Disk Writes, TUI)  │
                    └───────┬───────────────────────────────▲───────┘
                            │ Calls pure functions          │ Returns immutable
                            │ with input state              │ values / results
                    ┌───────▼───────────────────────────────┴───────┐
                    │                FUNCTIONAL CORE                │
                    │  • URL Validation & Sanitization              │
                    │  • Mathematical Chunk Division                │
                    │  • Exponential Moving Average (EMA) Speed     │
                    │  • Deterministic State Machine Transitions    │
                    │  • FIPS 180-4 Streaming SHA-256 Hashing       │
                    │  • Checkpoint Serialization / Deserialization │
                    └───────────────────────────────────────────────┘
```

### 1. Functional Core (`deox_core`)
- **Pure Functions**: Referentially transparent domain logic. Given the same inputs, functions always return identical outputs without side effects.
- **Monadic Result Types**: All operations that might fail return `std::expected<T, DeoxError>`. Exceptions are not used across domain boundaries.
- **Mathematical Invariants**: Chunk division guarantees zero byte gaps and zero byte overlaps with remainder-balanced segment distribution (`sum(chunk.length()) == total_size`).
- **Cryptographic Purity**: Built-in, self-contained FIPS PUB 180-4 compliant SHA-256 engine.

### 2. Imperative Shell (`deox_shell`)
- **Positional Thread-Safe Disk I/O**: Uses `pwrite(2)` directly on file descriptors, allowing multiple threads to write their downloaded slices concurrently without locking or serializing on file seek offsets.
- **Disk Preallocation**: Employs `posix_fallocate(2)` at initialization to reserve contiguous disk blocks, avoid file fragmentation, and immediately verify available disk capacity before consuming network bandwidth.
- **Atomic Checkpoint Persistence**: Checkpoint metadata (`.deox`) is written to temporary storage and committed using atomic POSIX `rename(2)`, ensuring zero corruption even during sudden power failure or crashes.
- **Adaptive Terminal UI**: Detects TTY mode to render interactive multi-line ANSI progress bars, or automatically falls back to clean, quiet milestone logs for CI/CD and piped streams.

---

## ⚖️ Cleanroom Licensing & Copyright Safety

Deox was authored completely cleanroom from first principles:
- **Zero GPL Contamination**: Unlike legacy tools such as `aria2` or `wget` (which are licensed under GPLv3), Deox is licensed under the permissive **MIT License**.
- **Commercial & Open Source Friendly**: Can be embedded, packaged, and distributed in proprietary or open-source ecosystems without viral copyleft obligations.
- **SPDX Standard Compliant**: Clear licensing annotations throughout all source headers.

---

## 🚀 Features

- ⚡ **Multi-Connection Segmented Acceleration**: Spawns multiple parallel workers utilizing HTTP/HTTPS `Range` requests.
- 💾 **Resumable Downloads**: Automatic crash-recovery checkpointing (`<file>.deox`). Resume interrupted downloads at the exact byte boundary.
- 🔒 **End-to-End Checksum Verification**: Streaming SHA-256 hash calculation directly from on-disk blocks without loading entire files into RAM.
- 🛡️ **Path Traversal Defense**: Strict URL and filename sanitization protecting against `../` path injection attacks.
- 📊 **Smoothed Throughput Tracking**: Exponential Moving Average (EMA) for stable transfer speed and accurate ETA prediction.
- 🧪 **100% ASan & UBSan Clean**: Verified against AddressSanitizer and UndefinedBehaviorSanitizer with zero leaks and zero memory errors.

---

## 🛠️ Building & Testing

### Prerequisites
- C++23 compliant compiler: `g++` (>= 14) or `clang++` (>= 18)
- `cmake` (>= 3.20)
- `libcurl` runtime (`libcurl.so.4`)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/DancinGhost/deox-downloadmanager.git
cd deox-downloadmanager

# Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build executable and libraries
cmake --build build -j$(nproc)

# Run test suite
./build/deox_tests
```

### Running with Sanitizers (ASan + UBSan)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build -j$(nproc)
./build/deox_tests
```

---

## 📖 Usage

```
Usage: deox [OPTIONS] <URL>

High-performance, memory-safe download manager in modern C++23.

Arguments:
  <URL>                      HTTP/HTTPS download target URL

Options:
  -o, --output <FILE>        Output target file path (default: extracted from URL)
  -c, --connections <NUM>    Parallel segment connections (default: 4, min: 1, max: 64)
  -s, --sha256 <HEX>         Expected SHA-256 checksum to verify upon completion
  -t, --timeout <SEC>        Network connection/transfer timeout in seconds (default: 30)
  -r, --retries <NUM>        Max chunk retries on network transient error (default: 3)
  -k, --insecure             Allow insecure TLS connections (skip certificate verification)
      --no-resume            Ignore existing checkpoint and restart download from scratch
  -q, --quiet                Quiet mode (suppress progress output, only show errors)
  -v, --version              Display version and licensing information
  -h, --help                 Display this help message
```

### Examples

```bash
# Basic download with 4 parallel connections
deox https://releases.ubuntu.com/24.04/ubuntu-24.04-desktop-amd64.iso

# High-concurrency download with custom output and SHA-256 validation
deox -c 16 -o linux.iso -s a1b2c3d4... https://example.com/linux.iso

# Resume interrupted download (automatic if .deox checkpoint is present)
deox https://example.com/huge-archive.tar.gz

# Quiet mode suitable for background cron scripts or Docker builds
deox -q -o package.tar.gz https://example.com/package.tar.gz
```

---

## 📜 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

Copyright (c) 2026 DancinGhost and Deox Contributors.
