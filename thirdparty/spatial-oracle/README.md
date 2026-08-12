# lean-spatial-oracle

The predictive spatial-oracle hexagon (formerly `PredictiveBvh`): ghost-expansion + SAH proofs (core), emitting `predictive_bvh.h` via the AmoLean codegen adapter (`lake exe bvh-codegen`).

> Split out of the [`lean-predictive-bvh`](https://github.com/v-sekai-multiplayer-fabric/lean-predictive-bvh) monorepo (now archived). Each hexagon cluster is its own repo following the `core/ports/adapters` convention; cross-cluster wiring is via Lake `require ... from git`.

## Dependencies

- [`lean-shared-core`](v-sekai-multiplayer-fabric/lean-shared-core) — common primitive types
- [`lean-rebac-core`](v-sekai-multiplayer-fabric/lean-rebac-core) — Formulas reference Relativistic (known core->core leak; TODO invert via a port)
- [`truth_research_zk`](https://github.com/V-Sekai-fire/truth_research_zk) — AmoLean E-graph optimizer + E-node/E-class storage

## Build

```sh
lake build         # production gate: typecheck the  cluster
lake build Research  # research-tier (non-gating; may fail)
```

## Hexagon layout

- `core/` — dependency-free domain logic + proofs
- `ports/` — narrow driving (source) / driven (sink) contracts
- `adapters/` — concrete I/O at the edges
