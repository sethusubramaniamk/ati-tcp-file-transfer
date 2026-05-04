# ftx — Secure Cross-Platform TCP File Transfer

A production-grade C++20 utility for transferring files of arbitrary size over TCP with end-to-end integrity, mutual TLS authentication, and resumable transfers. Built as the Ati Motors Platform Engineering assignment.

> Detailed architecture, threat model, performance numbers, and design-decision Q&A live in [**DESIGN.md**](DESIGN.md).

[![ci](https://github.com/sethu1209/ati-file-transfer/actions/workflows/ci.yml/badge.svg)](https://github.com/sethu1209/ati-file-transfer/actions/workflows/ci.yml)

## Features

- **Files up to 16 GiB** — validated end-to-end (16 GiB transferred in 142 s, 115 MiB/s, ~30 MiB RSS)
- **BLAKE3** per-chunk hashes + whole-file Merkle root, cross-checked against MANIFEST and COMPLETE
- **TLS 1.3 with mutual auth (mTLS)** via OpenSSL 3 — TLS-only by default; `--insecure` opts out for local testing
- **Resumable**: a `.ftxstate` sidecar lets interrupted transfers pick up exactly where they stopped, re-fetching only missing chunks
- **Path-traversal hardened**: receiver enforces a `--root` jail
- **Cross-platform**: single C++20 codebase, Linux / Windows / macOS; CI cross-compiles to ARM64 Linux (Jetson target)
- **Production hygiene**: 63 tests (unit + integration + fault-injection), ASan + UBSan in CI, `-Werror` gate

## Quick start (Linux / WSL)

```bash
# 1. Toolchain
sudo apt install -y build-essential ninja-build cmake pkg-config libssl-dev

# 2. Build
cmake --preset release
cmake --build --preset release

# 3. Run the test suite (63 tests)
ctest --preset release

# 4. Generate test certificates and try a TLS transfer
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

For local-only tinkering without TLS:

```bash
./build/release/src/ftx serve --listen 127.0.0.1:9000 --root /tmp/recv --insecure &
./build/release/src/ftx send  127.0.0.1:9000 myfile.bin --out delivered.bin --insecure
```

## Build matrix

| Preset             | What you get                                              |
| ------------------ | --------------------------------------------------------- |
| `release`          | -O3, no sanitizers — production binary                    |
| `debug`            | -O0 -g, full assertions                                   |
| `asan`             | Debug + AddressSanitizer + UBSan                          |
| `tsan`             | Debug + ThreadSanitizer                                   |
| `release-strict`   | Release + `-Werror` (used as the warnings gate in CI)     |

```bash
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

## Clean

Each preset writes into its own directory under `build/`. Remove a single preset's outputs, or all of them:

```bash
# clean a single preset (use the directory cmake created)
rm -rf build/release

# nuke all build outputs (preserves source, certs, deps cache)
rm -rf build/

# also drop the auto-generated test certificates and clear caches
rm -rf build/ certs/ .cache/
```

CMake's per-preset `clean` target works for object files (not for fetched deps):

```bash
cmake --build --preset release --target clean
```

## Cross-compile to ARM64 Linux (Nvidia Jetson)

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

Recent numbers on WSL2 / g++ 11.4 / loopback:

| chunk_size | throughput  |
| ---------: | ----------- |
| 64 KiB     | 111.5 MiB/s |
| 256 KiB    | 130.7 MiB/s |
| 1 MiB      | 133.3 MiB/s |
| 4 MiB      | 137.1 MiB/s |

See [DESIGN.md §8](DESIGN.md#8-performance) for the analysis.

## Layout

```
ati-file-transfer/
├── CMakeLists.txt              top-level build, sanitizer hooks, summary block
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
└── README.md                   ← you are here
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
  --sni HOST            client only — server name to send + verify against
  --insecure            disable TLS entirely (plain TCP — local testing only)

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

MIT — see [LICENSE](LICENSE).
