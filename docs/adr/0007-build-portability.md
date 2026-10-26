# ADR-0007: `-march=native` default and Python-embed build
Status: Accepted · 2026-10-26

## Context
The simulator is performance-sensitive and usually built and run on the same
machine, but it also embeds Python, so configuring needs Python 3 development
headers and NumPy.
## Decision
`CELLEVOX_NATIVE_OPTIMIZATIONS` defaults ON for local and benchmark builds.
Portable artifacts (Docker images, binaries moved between hosts) must configure
`-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF` to avoid `SIGILL`.
## Consequences
- CI and image builds must choose native versus portable explicitly.
- Changing build CPU/compiler invalidates performance comparisons.
- CI's `-DSKIP_GUI=ON` flag is a dead leftover, not a requirement; remove it
  once the benchmark baseline commit is refreshed. The Boost install has been
  removed.

## Evidence
- `CellEvoX/CMakeLists.txt` defines
  `option(CELLEVOX_NATIVE_OPTIMIZATIONS "..." ON)` and applies `-march=native`
  only to executable targets when it is enabled.
- Portable configuration is documented in `docs/runbook.md`
  (`-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF`) and is the required setting for the
  Docker image build.
- Build prerequisites, including the Python-embed requirement, are listed in
  `docs/runbook.md`.
