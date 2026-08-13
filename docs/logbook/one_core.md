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
  run time not at all -- **in the window this was measured over, which was from t=0 while the pile
  was still collapsing.** That is a real result and it is not the general statement it reads as.
  Re-measured across three scene shapes with repetitions, timing after the scene settles:

  | scene | timed from t=0 | timed after 200 settling steps | contacts after |
  | --- | ---: | ---: | ---: |
  | flat field, 900 cubes | 1.0x | **32.0x** | 3600 -> 0 |
  | pyramid, 204 cubes | 1.0x | **1115.8x** | 2851 -> 0 |
  | towers, 240 cubes | 1.0x | **4.4x** | 1040 -> 224 |

  Exactly 1.0x in all three while anything is moving, because nothing has fallen asleep yet.
  The towers row is the useful one: they never fully come to rest, contacts stop at 224 rather
  than 0, and the gain is 4.4x rather than a thousand. **The saving is not a property of the flag,
  it is a property of how completely the scene settles.**

  A sandbox is mostly settled -- somebody builds a stack and walks away -- so the steady-state
  cost is far below what 900 dynamic cubes suggests. What this does NOT rescue is the moment of
  collapse, which is where the 1840 ms tick came from and where sleep is worth exactly nothing.
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


## 2026-08-12: what a Godot humanoid costs, driven and simulated

"56 entities a player" is `SkeletonProfileHumanoid`, which is `bones.resize(56)` at
`scene/resources/skeleton_profile.cpp:489`, `root_bone = "Root"`. A skeleton, not fifty-six
loose objects, and two things follow that the archetype model assumed away.

**A player is atomic.** Fifty-six bones are one coupled kinematic chain; a forearm and its hand
cannot sit in different zones. The unit of assignment is an avatar, so 1400 entities is really
**twenty-five avatars**. Skeletons do not couple to each other, so zones still split freely at
player boundaries -- the same shape as ships passing at range, and the opposite of a stack.

**And the cost depends entirely on whether the bones are driven or simulated.**

| avatars | bones | driven | us/bone | simulated | us/bone | ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 56 | 0.019 ms | 0.34 | 1.181 ms | 21.08 | 62x |
| 5 | 280 | 0.068 ms | 0.24 | 5.959 ms | 21.28 | 88x |
| **25** | **1400** | **0.326 ms** | **0.23** | **33.666 ms** | **24.05** | **103x** |

Driven is **cheaper than the archetype model assumed** -- 0.23 us a bone against the 0.74 it
used, so that table was pessimistic by three times. A full zone of twenty-five avatars is 0.33
ms, seven tenths of one percent of the tick. It also gets cheaper per bone as avatars are added,
0.34 to 0.23, as the fixed per-step overhead amortises.

Simulated is a different cost class: **103x**, and a zone of twenty-five ragdolls is 33.7 ms,
**67% of the tick** before any props, game logic or fan-out.

Worth noticing where that lands. A simulated bone at 24.05 us is almost exactly a loose dynamic
cube at 21.40 us. **A ragdoll bone costs about what a physics crate costs**, which says the
articulation is not doing anything special -- it is fifty-five more bodies to solve.

So the entity budget is generous for driven avatars and cannot survive ragdolls at full
occupancy. That is a **concurrent-ragdoll limit** rather than a player limit, it is a normal
thing for a game to bound, and nothing here had named it. Ten simultaneous is 27% of the tick;
twenty-five is two thirds.

### What this does not say

- Self-collision is off in both cases, and the driven bones are `contype=0 conaffinity=0`, so
  neither figure includes an avatar colliding with anything. Adding that moves both.
- The bone lengths are approximate. The count and the chain depth are what cost; the
  millimetres are not.
- Ball joints with a 60-degree range and 0.5 damping, which is a plausible ragdoll and not a
  tuned one. A tuned one with tighter limits would differ.
- One run per configuration.


## 2026-08-13: freeze on settle — a dead end, then not one

`bench/freeze.{h,c}` promotes a settled body into the worldbody: copy its geoms to the world
at the pose it reached, delete the body, `mj_recompile`. Measured by `bench/freeze_test.c`,
which asserts rather than prints -- 300 boxes, every one freezes, and the tick goes from
**0.85 ms to 0.025 ms, 34x**. `mj_recompile` carries `mjData` across, so everything still
moving keeps its velocity and nothing jolts when a neighbour freezes.

### It is a zone transfer, and naming it that fixed a bug

A body leaves one owner and joins another, state has to survive, and there is a moment where
getting it wrong duplicates or loses the thing. That is `Fabric.lean`'s
`owned -> staging -> owned` with the world as the destination. It is the easy case only
because there is no message delay: the delete and the add happen in one spec edit, so it is
exactly-once by construction where a network handoff has to survive `u/2`.

Seeing that named a real defect. The first version froze bodies one at a time, so a settled
stack would transfer piecemeal -- the bottom becoming immovable while the top still leaned on
it, which is a different structure from the one that settled. **The unit of transfer is
whatever is internally coupled**, the same rule as a skeleton or a ship. `dof_island` already
computes the grouping, so an island is ripe only when every body in it is.

### Why it was a dead end as first written

`mjs_addGeom(world, NULL)` sets no name. **The transfer destroyed identity.** Once a piece was
in the worldbody nothing recorded what it had been, so it could not be unfrozen, deleted,
attributed to a builder, or described. Under "only things in the physics engine exist" that is
not a missing index somewhere else -- the object had stopped existing as an object and was now
scenery.

That made it a **one-way ratchet**. A world using it can only calcify, and a creative game
where nobody can move or remove what they built is not the game the survey asked for. Eight of
fourteen wanted building; none of them wanted building that sets.

### The fix, which was the same insight twice

**Transfers carry the entity across, they do not consume it** -- the rule that fixed the island
bug fixes this one. Each promoted geom is now named `body#g` (`WEFT_FROZEN_SEP`), which is
enough to find every piece of one thing again. `weft_thaw` is then the same transfer with the
endpoints swapped: collect the geoms carrying an identity, make a body at the first one's pose,
re-express the rest as offsets from it so a multi-geom thing keeps its shape, delete the world
copies, recompile.

`freeze_test.c` now asserts the round trip and returns non-zero if any leg fails:

    moved into world    : 300      every box transferred
    bodies left dynamic : 0
    recompiles          : 5
    thaw b7             : 1 geom(s) came back
    it is at            : -0.360 -0.900 0.050   the pose it settled at, not the authored one
    and it simulates    : yes      60 steps as a body
    tick 38x cheaper

So it is a **tier, not a ratchet**: a built world is affordable, and anything in it can be
picked back up.

### What this does not say

- The ratio is measured over a window because `clock()` has millisecond resolution and these
  ticks are far under one. Its first version reported FAIL against a working mechanism for
  exactly that reason. It also now suppresses the ratio entirely when the after-side falls
  below the timer's floor, because a divide by almost-zero prints `850000.0x` and that reads as
  a result rather than as an instrument running out. **A number an instrument cannot support is
  not a measurement, however good it looks.**
- Thaw is measured once, on one body, with nothing else moving. A world where players
  constantly grab things could thrash between tiers, and each crossing is a recompile. The
  hysteresis knob (`still_ticks`, 30 by default) is the only defence and is still untested
  against a grabbing player.
- Recompile cost against a large world is unmeasured. Five recompiles of a 300-body model were
  invisible in the tick; 40,000 frozen geoms may not be.
- Thaw caps at 32 pieces per identity and re-derives mass as a constant `0.2` rather than
  carrying the body's inertia across. A round trip therefore preserves shape and pose but not
  mass properties -- fine for the boxes here, wrong for anything a player tuned.

## Every run, as it was logged

`bench_players --log docs/logbook/one_core.md` appends here. The rows below are the raw
conditions and outcomes; the sections above are what they mean. Rows are never edited or
removed — a measurement that turned out to be wrong gets a section saying so.

| when             | run   | players | cubes | per player | entities | sim Hz | pub Hz | interest m | ticks | median ms | worst ms | simulate | encode | fanout | contacts |  sent |   bytes |
| ---------------- | ----- | ------: | ----: | ---------: | -------: | -----: | -----: | ---------: | ----: | --------: | -------: | -------: | -----: | -----: | -------: | ----: | ------: |
| 2026-08-12 07:09 | fixed |       4 |   900 |          3 |      912 |     60 |     20 |       10.0 |    60 |     21.12 |    58.47 |    20.83 |   0.04 |   0.01 |     3600 |   256 |   25600 |
| 2026-08-12 07:09 | fixed |     166 |   900 |          3 |     1398 |     60 |     20 |       10.0 |    60 |     20.53 |    26.96 |    19.59 |   0.06 |   0.45 |     3600 | 10624 | 1062400 |
| 2026-08-12 07:09 | fixed |     466 |     0 |          3 |     1398 |     60 |     20 |       10.0 |    60 |      3.98 |     5.63 |     1.04 |   0.04 |   3.02 |        0 | 29780 | 2978000 |
