# ADR-0005: Post-processing via Python subprocesses
Status: Accepted · 2026-10-26

## Context
Plots, phylogenies, Muller diagrams, and animations iterate faster in Python
(matplotlib, networkx, optional PyVista). The simulator embeds Python and invokes
scripts as child processes.
## Decision
C++ writes artifacts and delegates analysis to `CellEvoX/scripts`, resolving an
interpreter and script paths at runtime. Web and batch runs share the same
artifacts. `--postprocess exports` skips heavy plots and animations.
## Consequences
- Post-processing failures fall into three classes and must not be conflated:
  environmental (missing interpreter or package, `ffmpeg`, headless
  `MPLCONFIGDIR`), code defects in a script or in the C++→script boundary (for
  example the `f675045` `KeyError: 'ParentId'` from a column-name mismatch), and
  input-format mismatches (a run directory that lacks the columns a script
  expects). Only the environmental class is "not a simulation failure"; the other
  two are real bugs.
- Script resolution currently hardcodes DevContainer paths (`/workspaces/...`)
  and a `.venv` interpreter — known tech debt that must become relative or
  configurable.
- Some older invocations do not quote paths; spaces remain risky.

## Evidence
- Code-boundary defects: `f675045` "Fix crash/blank output in Muller & phylogeny
  plots for no-driver runs" (`KeyError: 'ParentId'` from a column-name
  mismatch) — a real bug, not an environment problem.
- Hardcoded paths in the current implementation:
  `CellEvoX/src/core/RunDataEngine.cpp` (`/workspaces/CellEvoX/CellEvoX/scripts`
  and `/workspaces/CellEvoX/.venv/bin/python`) and
  `CellEvoX/scripts/generate_test_data.py`. The fix is tracked separately as
  known tech debt, not as part of this decision.
- The three failure classes above are the accepted reading of post-processing
  failures; no further evidence is required for this ADR.
