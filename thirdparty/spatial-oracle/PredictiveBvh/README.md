# PredictiveBvh

The predictive spatial-oracle hexagon: ghost-expansion + SAH proofs (core), emitting predictive_bvh.h via the codegen adapter.

## Hexagon layout

- `core/` — dependency-free domain logic + proofs
- `ports/` — narrow driving (source) / driven (sink) contracts
- `adapters/` — concrete I/O at the edges

## Sibling wiring

- Shared — common primitive types
- Rebac — Formulas reference Relativistic; TODO invert via a port (known core->core leak)
