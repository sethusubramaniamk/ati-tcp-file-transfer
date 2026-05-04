# ftx

A C++20 utility for moving files between two machines over TCP. Does end-to-end integrity (BLAKE3 chunks plus a whole-file root hash), TLS 1.3 with mutual auth, and resumable transfers when the connection drops mid-flight.

Written for the Ati Motors Platform Engineering assignment.

> Architecture, threat model, perf numbers and the assignment Q&A live in [DESIGN.md](DESIGN.md).

[![ci](https://github.com/sethusubramaniamk/ati-tcp-file-transfer/actions/workflows/ci.yml/badge.svg)](https://github.com/sethusubramaniamk/ati-tcp-file-transfer/actions/workflows/ci.yml)

## What it does

- Streams files of arbitrary size. I tested 16 GiB end-to-end (142 s, 115 MiB/s, peak RSS ~30 MiB).
- BLAKE3 hashes per chunk, plus a whole-file root that gets cross-checked against MANIFEST and COMPLETE.
- TLS 1.3 with mutual auth. TLS is on by default; `--insecure` opts out for local poking.
- If a transfer dies, restart it. The receiver keeps a `.ftxstate` sidecar and only re-fetches the chunks it's missing.
- Path-traversal jail on the receiver (rejects `..`, absolute paths, paths that escape `--root`).
- One C++20 source tree, builds on Linux, Windows native, macOS, and cross-compiles to ARM64 Linux for Jetson.
- 63 tests (unit + integration + fault injection), ASan/UBSan in CI, `-Werror` gate.

## Quick start

| Platform | Toolchain | OpenSSL | Validated |
| --- | --- | --- | --- |
| **Linux (Ubuntu/Debian)** | `apt install build-essential ninja-build cmake pkg-config libssl-dev` | distro package | yes, primary; full suite + ASan/UBSan + 16 GiB e2e |
| **Linux (Fedora/RHEL)** | `dnf install gcc-c++ ninja-build cmake openssl-devel` | distro package | code-portable; CI matrix |
| **macOS** | `brew install ninja cmake openssl@3 pkg-config` | Homebrew (keg-only, see below) | code-portable; CI matrix |
| **Windows native** (MSVC) | Visual Studio 2022 Build Tools + CMake + Ninja + Git | `vcpkg install openssl:x64-windows` | yes, MSVC 19.44 + OpenSSL 3.6.2, 63/63 tests green locally |
| **Windows via WSL** | Same as Linux (Ubuntu) | distro package | yes, via the Linux path |

### Linux / WSL

```bash
sudo apt install -y build-essential ninja-build cmake pkg-config libssl-dev

cmake --preset release
cmake --build --preset release
ctest --preset release

# Generate test certificates and try a TLS transfer
./scripts/gen-test-certs.sh ./certs

# Receiver (terminal A)
./build/release/src/ftx serve \
    --listen 127.0.0.1:9000 \
    --root /tmp/ftx-recv \
    --tls-cert ./certs/server.crt \
    --tls-key  ./certs/server.key \
    --tls-ca   ./certs/ca.crt

# Sender (terminal B)
./build/release/src/ftx send 127.0.0.1:9000 ./BigFile.iso \
    --out delivered.iso \
    --tls-cert ./certs/client.crt \
    --tls-key  ./certs/client.key \
    --tls-ca   ./certs/ca.crt \
    --sni      localhost
```

Local-only without TLS:

```bash
./build/release/src/ftx serve --listen 127.0.0.1:9000 --root /tmp/recv --insecure &
./build/release/src/ftx send  127.0.0.1:9000 myfile.bin --out delivered.bin --insecure
```

### macOS

OpenSSL is keg-only on Homebrew, so CMake needs a hint:

```bash
brew install ninja cmake openssl@3 pkg-config
export OPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig"

cmake --preset release
cmake --build --preset release
ctest --preset release
```

### Windows (native, MSVC + vcpkg)

Open a Developer PowerShell for VS 2022 (or Developer Command Prompt). Either gives you the MSVC environment.

```powershell
# 1. One-time: install vcpkg + OpenSSL
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install openssl:x64-windows

# 2. Configure with the vcpkg toolchain
cmake -S . -B build\release -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows

# 3. Build + test
cmake --build build\release --config Release --parallel
ctest --test-dir build\release -C Release --output-on-failure
```

A few things to know about the Windows build:

The test-cert generator script is bash. Git for Windows (preinstalled on most dev boxes; otherwise `winget install Git.Git`) puts `bash.exe` and `openssl.exe` on PATH, and the script runs unmodified during the CMake build step. If you don't have Git for Windows the cert step will fail; either install it, pass `-DFTX_BUILD_TESTS=OFF` to skip the tests entirely, or generate certs yourself and pass them via `--tls-cert`/`--tls-key`/`--tls-ca`.

NTFS rejects `rename` onto an existing destination. `FileSink::finalize()` handles that with an explicit `remove + rename` retry.

NTFS doesn't have ext4-style sparse files, so the receiver's pre-allocated `.partial` is zero-filled instead of sparse. Functionally the same, just costs the disk space upfront.

### Windows via WSL2

If you already run WSL2 with Ubuntu, follow the Linux path above and you're done. The build artifacts are native Linux ELFs inside WSL; from Windows-side tools they live under `\\wsl$\Ubuntu\home\<you>\...`.

## Build presets

| Preset             | What it gives you                                         |
| ------------------ | --------------------------------------------------------- |
| `release`          | -O3, no sanitizers                                        |
| `debug`            | -O0 -g, full assertions                                   |
| `asan`             | Debug + AddressSanitizer + UBSan                          |
| `tsan`             | Debug + ThreadSanitizer                                   |
| `release-strict`   | Release + `-Werror` (the warnings gate in CI)             |

```bash
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

## Clean

Each preset writes into its own subdirectory under `build/`. Pick one to clear, or wipe everything:

```bash
# clean a single preset
rm -rf build/release

# nuke all build outputs (preserves source, certs, deps cache)
rm -rf build/

# also drop the auto-generated test certificates and clear caches
rm -rf build/ certs/ .cache/
```

CMake's per-preset `clean` target works for object files but not the fetched deps:

```bash
cmake --build --preset release --target clean
```

## Cross-compile to ARM64 Linux (Jetson)

```bash
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

cmake -S . -B build/cross-aarch64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DFTX_BUILD_TESTS=OFF

cmake --build build/cross-aarch64
file build/cross-aarch64/src/ftx          # ELF 64-bit LSB pie executable, ARM aarch64
```

## Performance probe

```bash
cmake --preset release -DFTX_BUILD_BENCH=ON
cmake --build --preset release
./build/release/bench/ftx_bench 256       # 256 MiB transfer × 4 chunk sizes
```

Numbers from my box (WSL2, g++ 11.4, loopback):

| chunk_size | throughput  |
| ---------: | ----------- |
| 64 KiB     | 111.5 MiB/s |
| 256 KiB    | 130.7 MiB/s |
| 1 MiB      | 133.3 MiB/s |
| 4 MiB      | 137.1 MiB/s |

The analysis (and what's leaving perf on the table) is in [DESIGN.md §8](DESIGN.md#8-performance).

## Layout

```
ati-file-transfer/
├── CMakeLists.txt              top-level build, sanitizer hooks, summary
├── CMakePresets.json           release / debug / asan / tsan / release-strict
├── .github/workflows/ci.yml    Linux x64 (×3 modes) + aarch64 cross-compile
├── cmake/toolchains/           aarch64-linux-gnu.cmake
├── include/ftx/
│   ├── proto/                  types, frame, decoder, messages
│   ├── transport/              connection (variant<TCP,TLS>), tls, server, client
│   ├── io/                     file_source, file_sink, resume_state
│   ├── util/                   crc32c, blake3, byteorder
│   └── version.hpp
├── src/                        ftx_lib (.a) + ftx (CLI)
├── tests/                      unit + integration suites
├── third_party/                FetchContent: GoogleTest, spdlog, CLI11, Asio, BLAKE3
├── bench/                      throughput probe
├── scripts/                    gen-test-certs.sh, smoke_e2e.sh
├── DESIGN.md                   architecture, threat model, perf, design Q&A
└── README.md                   (you are here)
```

## CLI reference

```text
ftx — secure cross-platform TCP file transfer
Usage: ftx [OPTIONS] SUBCOMMAND

Subcommands:
  serve     Run as receiver (server)
  send      Send a file to a remote ftx serve

Common TLS options (both subcommands):
  --tls-cert PATH       PEM certificate (chain) for this side
  --tls-key  PATH       PEM private key for --tls-cert
  --tls-ca   PATH       PEM bundle of trusted CAs (peer verification)
  --no-verify-peer      do not require / verify a peer certificate
  --sni HOST            client only, server name to send + verify against
  --insecure            disable TLS entirely (plain TCP, local testing only)

ftx serve --listen H:P --root DIR
ftx send  H:P SOURCE [--out PATH] [--chunk-size BYTES]
```

## Tests

```text
$ ctest --preset release
Total Test time (real) =   3.32 sec
100% tests passed, 0 tests failed out of 63
```

Coverage by suite:

| Suite                     | Count | What it covers                                                   |
| ------------------------- | ----: | ---------------------------------------------------------------- |
| `Smoke`                   |     2 | sanity checks                                                    |
| `Crc32c`                  |     5 | reference vectors + chained-equals-concat property               |
| `Blake3`                  |     5 | reference vectors, incremental == one-shot, reset, bit-flip      |
| `FrameHeader`             |     7 | encode/decode roundtrip, rejection of bad CRC / unknown / oversized |
| `FrameDecoder`            |     8 | streaming feeds: bytewise, multiple-frames, residual, poison-on-fail |
| `Hello/Manifest/Chunk/Complete/Ack/Error` | 12 | message codec roundtrips + truncation rejection             |
| `ResumeState`             |     6 | bitmap math + disk roundtrip + corruption rejection              |
| `Loopback`                |     9 | end-to-end transfers from 0 B up to 64 MiB                       |
| `TlsLoopback`             |     3 | mTLS roundtrip, mTLS rejection without client cert, untrusted-server |
| `FaultInjection`          |     3 | resume-no-op, stale-state-recovery, chunk-tampering rejection    |
| `LargeTransfer`           |     1 | gated multi-GiB e2e (FTX_LARGE_TEST=1, sized via FTX_LARGE_TEST_SIZE_GIB; validated to 16 GiB) |

## License

MIT, see [LICENSE](LICENSE).
