# ADR-0007: `-march=native` default and Python-embed build
Status: Accepted · 2026-10-24

## Context
The simulator is performance-sensitive and usually built and run on the same
machine, but it also embeds Python, requiring development headers and NumPy at
configure time. Qt has been removed and is not a dependency.
## Decision
`CELLEVOX_NATIVE_OPTIMIZATIONS` defaults ON for local and benchmark builds.
Portable artifacts (Docker images, binaries moved between hosts) must configure
`-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF` to avoid `SIGILL`.
## Consequences
- CI and image builds must choose native versus portable explicitly.
- Changing build CPU/compiler invalidates performance comparisons.
- CI's `-DSKIP_GUI=ON` and Boost install are dead leftovers, not requirements;
  remove them once the benchmark baseline commit is refreshed.
