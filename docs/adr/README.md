# Architecture Decision Records

ADRs capture *why* a non-obvious choice exists. They are not tutorials and must
not restate code, schemas, or control flow. If a decision is later reversed,
mark the ADR `Superseded by ADR-XXXX` instead of deleting it.

Every ADR needs its `Evidence` section filled in before it moves from `Proposed`
to `Accepted`. ADR-0003 and ADR-0004 are still `Proposed`: ADR-0003 is debt the
owner has acknowledged, and ADR-0004 is blocked on the cross-language snapshot
fixture. The rest were accepted on 2026-10-26.

| ADR | Status | Decision |
| --- | --- | --- |
| [0001](0001-determinism.md) | Accepted | Counter-based RNG for thread-count-independent results |
| [0002](0002-event-semantics.md) | Accepted | Birth replaces the parent with two daughters |
| [0003](0003-dual-storage-engines.md) | Proposed — debt acknowledged | Dense 2D storage and the duplicated event loop |
| [0004](0004-snapshot-abi.md) | Proposed | Binary snapshot ABI, one coherent contract per commit |
| [0005](0005-postprocessing-subprocess.md) | Accepted | Post-processing via Python subprocesses |
| [0006](0006-web-runner-model.md) | Accepted | Single global runner with isolated batch outputs |
| [0007](0007-build-portability.md) | Accepted | `-march=native` default and Python-embed build |
| [0008](0008-performance-gate.md) | Accepted | Paired benchmark gate and phase profiling |
| [0009](0009-interleaved-benchmark-rounds.md) | Proposed | Interleaved base/head rounds, median of round means |

## Template

```markdown
# ADR-NNNN: <decision>
Status: Proposed | Accepted | Superseded by ADR-XXXX
Date: YYYY-MM-DD
Context: the external force, constraint, or failure that forced a choice
Decision: what we do
Consequences: what becomes forbidden; what breaks if changed
Evidence: commit, benchmark, incident, or code pointer
```
