# CellEvoX Documentation

This directory is the project knowledge layer. It is written for future agents and
contributors who need to understand the repo quickly without reverse-engineering
the whole codebase every time.

## Start here

Read in this order when you are new to the project:

1. [Agent onboarding](agent-onboarding.md) - first-pass map, commands, and safe working rules.
2. [Project knowledge base](PROJECT_KNOWLEDGE_BASE.md) - compact briefing of the current system.
3. [Architecture](architecture.md) - how C++, web, scripts, and output flow together.
4. [Simulation engines](simulation-engines.md) - model semantics and engine-specific invariants.
5. [Config fields by mode](config-fields-by-mode.md) - schema and mode-specific config behavior.
6. [Run output and analysis](run-output-and-analysis.md) - run directory layout, snapshots, CSV, plots, web results.
7. [Development workflows](development-workflows.md) - build, test, benchmark, web, and review routines.
8. [High-risk areas](high-risk-areas.md) - files and contracts that need extra verification.
9. [Batch runs](batch-runs.md) - console and web batch-run behavior.

## Task routing

| Task | Read first | Verification expectation |
| --- | --- | --- |
| Change simulation behavior | [Simulation engines](simulation-engines.md), [High-risk areas](high-risk-areas.md) | Unit tests plus deterministic/parity checks that cover the touched engine |
| Optimize performance | [Development workflows](development-workflows.md), [High-risk areas](high-risk-areas.md) | Benchmark before and after every meaningful change |
| Add or rename config fields | [Config fields by mode](config-fields-by-mode.md), [Architecture](architecture.md) | C++ parser, backend schema, frontend types/defaults, imported/exported payloads |
| Change output files | [Run output and analysis](run-output-and-analysis.md), [High-risk areas](high-risk-areas.md) | C++ writer/reader, Python loader/parser, web results parser, snapshot tests |
| Work on web UI/backend | [Architecture](architecture.md), [Batch runs](batch-runs.md), [Development workflows](development-workflows.md) | Frontend build/lint when available, backend smoke path, binary path assumptions |
| Investigate flaky correctness | [Simulation engines](simulation-engines.md), [High-risk areas](high-risk-areas.md) | Seeded repeatability tests, single-thread TBB control where determinism is required |

## Source of truth rules

- Code is authoritative. Docs should explain the current code, not the intended future design.
- If a config, output format, engine mode, or workflow changes, update the relevant doc in the same PR.
- The top-level [README](../README.md) is a product entry point. Detailed engineering context lives here.
- Do not leave stale generated notes as the only explanation. Convert useful findings into one of these docs.

## Current baseline

This docs set describes the `main` lineage after the web frontend merge visible at
commit `a26ea22` in this workspace. The branch for this documentation work is
`docs/agent-knowledge-base`.

Important current status:

- The implemented production simulation paths are stochastic 2D, spatial 3D density, and spatial 3D capacity.
- The public config/web schema includes `deterministic`, and C++ parses it as `DETERMINISTIC_RK4`, but the current `SimulationEngine::step()` path does not execute an RK4 implementation.
- `SKIP_GUI=ON` is the normal test build path and builds `CellEvoXTests`; the runnable `CellEvoX` binary requires the non-GUI skip path plus Qt dependencies.
- Performance-sensitive changes must be benchmarked before and after. Treat Amdahl's law as a design gate, especially around `CommonPopulationStep`, snapshots, and memory logging.
