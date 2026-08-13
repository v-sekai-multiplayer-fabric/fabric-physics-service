# One core logbook

What a zone tick costs on one core, with the conditions it ran under. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

Split out of this file as it grew: [determinism.md](determinism.md), [scaling.md](scaling.md), [curves.md](curves.md), [topology.md](topology.md), [filming.md](filming.md), [borrowed_tricks.md](borrowed_tricks.md).

## Apparatus

Unless an entry says otherwise:

- Host: this desk. Windows 11, MSVC, Release, one core, pinned with `SetThreadAffinityMask`.
  It is the machine the headset is on, which is the reason to measure here at all.
- MuJoCo 3.11.0, from `thirdparty/mujoco-riscv64`, simulating at 60 Hz and publishing at 20.
- The packet is `XRGridEntityPacket`, 100 integral bytes, from `thirdparty/entity-packet`.
- The fan-out is `fanout_one` from `thirdparty/fanout-edge`, unmodified, with a counting sink
  where a transport would be. Ghost-AABB overlap, capped at `MAX_SLICE_ENTITIES` 64.
- A player is 3 entities: a head and two hands, which is the article's own avatar.
- The budget is 50 ms, which is one tick at 20 Hz on one core.

The scene is a floor, a grid of free-joint cubes, and three mocap bodies per player moving in a
slow circle through the field. Cubes are simulated; avatars are driven, as they are in the
article.

## 2026-08-12: the ward runs out before the core does

Three shapes, sixty ticks each.

| players | cubes | entities |   median | of budget | simulate | encode | fanout |
| ------: | ----: | -------: | -------: | --------: | -------: | -----: | -----: |
|       4 |   900 |      912 | 21.12 ms |       42% |    20.83 |   0.04 |   0.01 |
|     166 |   900 |     1398 | 20.53 ms |       41% |    19.59 |   0.06 |   0.45 |
|     466 |     0 |     1398 |  3.98 ms |        8% |     1.04 |   0.04 |   3.02 |

Run to run the cubed medians move by about 2 ms and the worst tick by much more — 58 ms in one
of the four-player runs, which is over budget on its own. That spread is a desktop with a
browser open, and it is the reason the sections below argue from ratios between stages rather
than from any single figure.

**166 players fit beside 900 cubes, and the entity budget is what stopped it, not the core.**
`WARD_AUTHORITY` is 1400 and `900 + 166 × 3` is 1398; the ramp reached the last player the ward
could hold with 59% of the tick unspent. The answer to "how many players on one core" is
therefore not a CPU number at this scene size. It is `(1400 - cubes) / 3`.

**Cubes cost, players do not.** Going from 4 players to 166 — five hundred more entities to
simulate, encode and filter — moved the median by less than a millisecond, well inside the
run-to-run spread. Removing the 900 cubes took the simulate stage from 20 ms to 1.0. What the physics is paying for is 3600
contacts, and contacts come from cubes resting on each other and the floor, not from people.

**Encode is free and should stop being discussed as though it were not.** 1398 packets, 139 800
bytes, in 0.04 ms. The integral layout has no allocation and no conversion beyond a
double-to-int64 per axis.

**The fan-out grows with players and overtakes the physics.** 0.01 ms at 4 players, 0.45 at 166,
3.02 at 466. It is O(players × entities) and it is the only stage that is, so it is the stage
that decides the shape of any zone with more people than props.

### The limit is the NIC, not the core

The interest filter passed a full slice to every subscriber in all three runs — 64.0, 64.0, 63.9
against a cap of 64. With 900 cubes packed at 0.6 m and an interest box of 10 m, everything near
a subscriber overlaps, so what bounds a slice is `MAX_SLICE_ENTITIES` and not the geometry.

That makes the egress arithmetic fixed per subscriber: 64 × 100 bytes × 20 Hz is **1.02 Mbps,
uncompressed**. At 166 players that is 170 Mbps, and at 466 it is 477 Mbps — from one core that
was 8% busy.

The article's whole four-player host fitted in about 1 Mbps, and under 256 kbps a player,
because it delta-compressed against an acknowledged baseline. We send absolute state. **So the
next thing worth measuring is not more players on a core; it is what a delta costs.** A core
with 92% of its tick free is not the constraint, and adding cores will not help a NIC.

### What this does not say

- Nothing here ran on a zone host. This is one desktop core, Windows, MSVC.
- No transport. The sink counts bytes; it does not send them, so nothing above measures what
  QUIC, pacing or retransmission add.
- No authority or ownership sequences, and no prediction. The article's mechanism is not
  modelled — this measures whether the budget is reachable at all.
- The 900 cubes are a settled grid, not the twenty-metre stacks the article describes. A stack
  is a harder contact problem and the simulate stage would cost more.
- Contacts were 3600 in every cubed run, which is the field at rest. A session where players
  are actually throwing things has a moving contact count this scene does not produce.

## 2026-08-12: what a stacked scene costs, and why the answer is not geometry

Left in the open after the entries above: whether the flat field flatters the numbers, since a
zone that has been played in is not a settled grid. It does, and the correction is larger and in
a different direction than expected.

Measured, one core, 900 cubes unless stated, wall clock against simulated time:

| scene | realtime | note |
| --- | ---: | --- |
| flat field, 900 cubes, 4 players | **4.15×** | comfortably inside the budget |
| 100 cubes, 6-layer pyramid | 0.92× | 1345 contacts |
| 200 cubes, 8-layer pyramid | **0.11×** | 3128 contacts |
| 400 cubes, 10-layer pyramid | **0.02×** | 6435 contacts |

**Stability and speed are opposed.** A pyramid stands because every cube rests on others, which
is the maximally-contacting arrangement: 100 cubes make 1345 contacts, 400 make 6435. The shape
that does not fall over is the shape that costs the most.

A column is worse and fails differently. Fifty 0.4 m cubes is an aspect ratio of fifty to one,
and nothing stands at fifty to one. It does not topple, it ejects — the top cube leaves 19.9 m
and reaches 23 m within two seconds, with the gap between levels closed to zero.

### The knobs that do not work

- **Solver iterations.** 100 → 20 → 10 → 5 moved the worst tick by noise, and 10 came out worse
  than 100. There is no solver problem to cap.
- **Sleeping.** `<flag sleep="enable"/>` takes the reported contact count to zero and changes the
  run time not at all. That is the informative result: if contacts go to zero and the cost stays,
  the cost was never in the contact solve. It is in collision detection, which still runs.

Both point the same way, and it is the same way the iteration result pointed. The time is spent
before the solver.

### So the bound has to be structural

A sandbox takes adversarial input. Whatever shape is chosen here, a player can build the one that
is worst, so "we picked a stable geometry" is not a defence and neither is any amount of tuning.
Two mechanisms, and only the second is a guarantee:

- **Weld settled assemblies.** A stack that has not moved for some frames becomes one body, which
  is what the large commercial sandboxes do and what turns 1345 contacts into a handful. MuJoCo
  can do it: `mjSpec` and
  `mj_compile` allow the model to be edited and recompiled at runtime. It is deterministic — the
  same state welds the same way everywhere — so the weld rule joins the wire contract beside the
  simulation rate.
- **Time-box the tick.** Budget the wall clock inside the tick, drop substeps when it is spent,
  publish anyway. Fidelity degrades and the deadline never does. This is the only bound a player
  cannot out-build, and the welding above is an optimisation under it rather than a replacement.

### What this does not say

- Every figure is one desktop core, Windows, MSVC, one thread.
- The pyramid geometry that produced these numbers is not in the tree. It was reverted rather
  than committed: a scene shape known to miss the budget by fifty times is one somebody would
  otherwise benchmark with by accident.
- Nothing here measures welding or a time-box. Both are proposed on the strength of what the
  knobs above failed to do, not on a measurement of what they achieve.
- The three-topology comparison does not wait on any of this. It runs on the flat field, which
  holds 4.15×, and all three topologies integrate whatever the engine ends up costing.

## 2026-08-12: a cube is ten players, and the wall is the island

A reading taken from the entries above was wrong, and wrong in a way worth writing down because
the arithmetic looked fine. The first entry says 166 players fit at 41% of the tick, and 41% was
read as 59% of headroom to sell. It is not. The marginal cost of a player was taken from the
`466 players, 0 cubes` row — a scene in which **the players have nothing to touch** — and then
spent in a scene where they would be touching constantly. Pricing the free case and billing the
expensive one.

Repricing the same three rows against what each entity actually costs:

| | marginal cost | entity budget charges it |
| --- | ---: | ---: |
| one cube | **21.4 µs** | 1 |
| one player (3 mocap entities) | **2.2 µs** | 3 |

**A cube costs ten times a player and is billed a third as much.** `WARD_AUTHORITY` is a uniform
count over non-uniform costs, so every player admitted displaces budget it does not use and every
cube consumes budget it is not charged for.

### The variable is island size, not entity or contact count

Reading the stacked-scene entry beside the flat-field one gives the real law. 900 cubes on a flat
field are 900 independent islands of one body; a 204-cube pyramid is one island. Per body:

| island size K | µs per body | source |
| ---: | ---: | --- |
| 1 | 21.4 | measured, flat field |
| 100 | 543 | measured, 6-layer pyramid |
| 204 | 6250 | measured, 10-layer pyramid |

**100 coupled cubes cost more than 900 uncoupled ones.** Fitting the first two points gives
`cost/body ≈ 21.4 · K^0.70 µs`, and at a 45 ms budget that is a design law:

    bodies × K^0.70 ≤ 2103

| island cap K | max simulated bodies |
| ---: | ---: |
| 1 | 2102 |
| 3 | 972 |
| 10 | 417 |
| 20 | 256 |
| 100 | 82 |

The fit is three points and it **stops being a power law at the top**: K=204 measured 6250 µs
against 896 predicted, seven times worse. Past about K=100 it is a cliff, not a curve, and should
be treated as infeasible rather than extrapolated.

### Which is why locked volumes are worth more than welding

The stacked-scene entry proposed welding and a time-box, and said plainly that neither was
measured. A third mechanism is better than both and needs no measurement to justify, because it
is a rule rather than a behaviour: **if an entity can only be edited inside a volume its editor
has locked, then bodies outside locked volumes are static** — infinite-mass anchors that
terminate an island instead of propagating it. The lock volume *is* the island cap, `K ≤ E`, and
it cannot be exceeded by any input. It also kills island merging, which welding does not: two
disjoint volumes cannot couple.

900 bodies at E=3 is 41.6 ms and fits; at E=5 it is 59.4 ms and does not. So the cost is legible
and paid in gameplay — **stacks are three high** — rather than paid in dropped ticks by everyone
in the zone. That is the same limit welding would impose, moved to where a player can see it.

### What this does not say

- The 21.4 / 2.2 µs split and the three island points are re-readings of runs already in the
  table below. **No new run was taken for this entry.**
- `K^0.70` is fitted from two points and contradicted by the third. It is a design aid, not a
  model. The measurement that would settle it is island size against tick cost swept over
  K = 1..30, which nothing has run.
- Locked volumes are not implemented and not measured. The claim here is that the bound is
  *enforceable*, which is an argument about the mechanism, not evidence about its cost.
- Lock acquisition is a distributed mutex and has its own floor — see the entry below.

## Every run, as it was logged

`bench_players --log docs/logbook/one_core.md` appends here. The rows below are the raw
conditions and outcomes; the sections above are what they mean. Rows are never edited or
removed — a measurement that turned out to be wrong gets a section saying so.

| when             | run   | players | cubes | per player | entities | sim Hz | pub Hz | interest m | ticks | median ms | worst ms | simulate | encode | fanout | contacts |  sent |   bytes |
| ---------------- | ----- | ------: | ----: | ---------: | -------: | -----: | -----: | ---------: | ----: | --------: | -------: | -------: | -----: | -----: | -------: | ----: | ------: |
| 2026-08-12 07:09 | fixed |       4 |   900 |          3 |      912 |     60 |     20 |       10.0 |    60 |     21.12 |    58.47 |    20.83 |   0.04 |   0.01 |     3600 |   256 |   25600 |
| 2026-08-12 07:09 | fixed |     166 |   900 |          3 |     1398 |     60 |     20 |       10.0 |    60 |     20.53 |    26.96 |    19.59 |   0.06 |   0.45 |     3600 | 10624 | 1062400 |
| 2026-08-12 07:09 | fixed |     466 |     0 |          3 |     1398 |     60 |     20 |       10.0 |    60 |      3.98 |     5.63 |     1.04 |   0.04 |   3.02 |        0 | 29780 | 2978000 |
