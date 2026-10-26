# Architecture Decision Records

ADRs capture *why* a non-obvious choice exists. They are not tutorials and must
not restate code, schemas, or control flow. If a decision is later reversed,
mark the ADR `Superseded by ADR-XXXX` instead of deleting it.

All ADRs below are currently `Proposed`: they describe the intended contract but
have not yet been signed off by the domain owner. Each one needs its `Evidence`
section filled in before it moves to `Accepted`.

| ADR | Decision |
| --- | --- |
| [0001](0001-determinism.md) | Counter-based RNG for thread-count-independent results |
| [0002](0002-event-semantics.md) | Birth replaces the parent with two daughters |
| [0003](0003-dual-storage-engines.md) | Dense 2D storage and the duplicated event loop |
| [0004](0004-snapshot-abi.md) | Binary snapshot ABI and backward compatibility |
| [0005](0005-postprocessing-subprocess.md) | Post-processing via Python subprocesses |
| [0006](0006-web-runner-model.md) | Single global runner with isolated batch outputs |
| [0007](0007-build-portability.md) | `-march=native` default and Python-embed build |
| [0008](0008-performance-gate.md) | Paired benchmark gate and phase profiling |

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
