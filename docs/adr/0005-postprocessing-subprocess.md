# ADR-0005: Post-processing via Python subprocesses
Status: Proposed · 2026-10-24

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
To be recorded before this ADR moves to `Accepted` (failure log excerpts for each
class, including `f675045`).
