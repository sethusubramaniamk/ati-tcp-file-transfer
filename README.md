# ftx — Secure Cross-Platform File Transfer

A production-grade C++20 utility for transferring files over TCP with end-to-end integrity, mutual TLS authentication, and resumable transfers. Built for the Ati Motors Platform Engineering assignment.

> Status: scaffolding (phase 0). Functional implementation lands in subsequent phases.

## Features (target)

- Files up to 16 GB streamed with constant memory
- BLAKE3 chunk hashes + whole-file Merkle root for end-to-end integrity
- TLS 1.3 with mutual authentication (mTLS)
- Resumable transfers — interrupted sessions resume from missing chunks only
- Cross-platform: Linux, Windows, macOS (single C++20 codebase, ASIO + OpenSSL)
- Optional zstd compression with auto-skip heuristic
- Bounded backpressure pipeline — RSS stays flat regardless of file size

## Quick build (Linux / WSL)

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Binary lands at `build/release/src/ftx`.

## Documentation

- [DESIGN.md](DESIGN.md) — architecture, C4 diagrams, threat model, perf table, design-decision log

## Layout

```
ati-file-transfer/
├── CMakeLists.txt          top-level build
├── CMakePresets.json       release / debug / asan / tsan
├── include/ftx/            public headers
├── src/                    library + CLI
│   ├── lib/                ftx_lib
│   └── cli/                ftx executable
├── tests/                  GoogleTest unit + integration
├── third_party/            FetchContent declarations
├── scripts/                cert-gen, dev helpers
└── docs/                   ancillary docs
```

## License

MIT — see [LICENSE](LICENSE).
