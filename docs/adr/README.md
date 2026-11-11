# Architecture Decision Records

ADRs capture *why* a non-obvious choice exists. They are not tutorials and must
not restate code, schemas, or control flow. If a decision is later reversed,
mark the ADR `Superseded by ADR-XXXX` instead of deleting it.

Every ADR needs its `Evidence` section filled in before it moves from `Proposed`
to `Accepted`. ADR-0003 was accepted on 2026-11-04; ADR-0004 was accepted on
2026-11-11 once the cross-language snapshot fixture landed (#52); ADR-0009 is
also `Proposed`. Acceptance dates are recorded in each ADR.

| ADR | Status | Decision |
| --- | --- | --- |
| [0001](0001-determinism.md) | Accepted | Counter-based RNG for thread-count-independent results |
| [0002](0002-event-semantics.md) | Accepted | Birth replaces the parent with two daughters |
| [0003](0003-dense-population-step.md) | Accepted | Dense stochastic population step with an optional xyz overlay |
| [0004](0004-snapshot-abi.md) | Accepted | Binary snapshot ABI, one coherent contract per commit |
| [0005](0005-postprocessing-subprocess.md) | Accepted | Post-processing via Python subprocesses |
| [0006](0006-web-runner-model.md) | Accepted | Single global runner with isolated batch outputs |
| [0007](0007-build-portability.md) | Accepted | `-march=native` default and Python-embed build |
| [0008](0008-performance-gate.md) | Accepted | Paired benchmark gate and phase profiling |
| [0009](0009-interleaved-benchmark-rounds.md) | Proposed | Interleaved base/head rounds, median of round means |
| [0010](0010-gamma-mutation-effect.md) | Accepted | Gamma-distributed mutation effects on a dedicated RNG stream |

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
