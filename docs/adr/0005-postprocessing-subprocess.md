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
- Interpreter and script paths resolve at runtime: `CELLEVOX_PYTHON`, then the
  interpreter captured at configure time, then `python3`; scripts come from the
  working directory, the configured source tree, then the `__FILE__`-derived
  path. A moved binary therefore degrades to `python3` plus cwd-relative scripts
  instead of assuming a container layout.
- Scripts are invoked through one quoting helper, so paths with spaces are
  handled.

## Evidence
- Code-boundary defects: `f675045` "Fix crash/blank output in Muller & phylogeny
  plots for no-driver runs" (`KeyError: 'ParentId'` from a column-name
  mismatch) — a real bug, not an environment problem.
- Script/interpreter resolution used to hardcode DevContainer paths
  (`/workspaces/...`); `6313efe` replaced them with configure-time definitions
  and a `CELLEVOX_PYTHON` override, so runs no longer depend on the container
  layout.
- The three failure classes above are the accepted reading of post-processing
  failures; no further evidence is required for this ADR.
