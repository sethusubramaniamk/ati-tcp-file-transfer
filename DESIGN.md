# ftx — Design Document

> Living document. Updated at the end of each implementation phase.

## 1. Context

`ftx` is a CLI utility that transfers files over TCP between two hosts. One end runs `ftx serve` (the receiver / server); the other runs `ftx send` (the sender / client). Designed for files up to 16 GB, integrity-checked end-to-end, encrypted and mutually authenticated on the wire.

The problem statement comes from the Ati Motors Platform Engineering assignment. The wider context — Ati ships software updates to a fleet of Jetson-based autonomous mobile robots — is reflected in the design choices: TLS-with-mTLS for fleet authentication, BLAKE3 manifests for OTA-style payload verification, resumability for unreliable links, and bounded memory for resource-constrained edge hardware.

## 2. Goals & non-goals

### Goals
- Transfer files of arbitrary size (validated up to 16 GB) with constant memory.
- End-to-end integrity: every chunk verified on receive, whole-file Merkle root verified on completion.
- Authentication and confidentiality on the wire (TLS 1.3 + mTLS).
- Resumable on connection drop, sender restart, or receiver restart.
- Cross-platform: Linux, Windows, macOS, single source tree.
- Production hygiene: tests, sanitizers, CI, docs.

### Non-goals (v1)
- Multi-recipient broadcast.
- Directory recursion (single-file at a time).
- Replay protection across separate sessions (single-session integrity only).
- Bandwidth shaping.
- Federation / discovery.

## 3. Architecture

_To be expanded with C4 Context / Container / Component diagrams as implementation progresses._

## 4. Wire protocol

_To be expanded in phase 1._

## 5. Threat model

_To be expanded in phase 4._

## 6. Performance

_Numbers measured in phase 6+._

## 7. Design decisions

Each "should I…?" question raised by the assignment, with an answer:

### Should I break the file into smaller chunks?
_Pending phase 1._

### Should I compress the file before transferring?
_Pending phase 6._

### Error handling and retries
_Pending phase 5._

### Economy of system resource utilization
_Pending phase 6._

## 8. Trade-offs we explicitly accepted

- **Project lives on Windows NTFS, builds run from WSL2 across the `/mnt/d/` boundary.** A 2-5× I/O penalty per syscall, negligible for a CPU-bound C++ build at this scale. Chosen to keep tooling access simple on the deadline.
- **Windows + macOS validated via CI, not on developer's box.** Linux is the primary platform (matches Ati's deployment target — Jetson Linux). CI matrix covers the others.
- **No vcpkg.** All third-party deps via `FetchContent` with pinned tags. Simpler reproducibility story, fewer toolchain assumptions.
