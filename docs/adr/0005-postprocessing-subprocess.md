# ADR-0005: Post-processing via Python subprocesses
Status: Accepted · 2026-10-24

## Context
Plots, phylogenies, Muller diagrams, and animations iterate faster in Python
(matplotlib, networkx, optional PyVista). The simulator embeds Python and invokes
scripts as child processes.
## Decision
C++ writes artifacts and delegates analysis to `CellEvoX/scripts`, resolving an
interpreter and script paths at runtime. Web and batch runs share the same
artifacts. `--postprocess exports` skips heavy plots and animations.
## Consequences
- Failures are environmental (interpreter, packages, `ffmpeg`, headless
  `MPLCONFIGDIR`), not simulation failures.
- Script resolution currently hardcodes DevContainer paths (`/workspaces/...`)
  and a `.venv` interpreter — known tech debt that must become relative or
  configurable.
- Some older invocations do not quote paths; spaces remain risky.
