# One core logbook

Every measurement of what a zone tick costs, with the conditions it ran under.

A number without its conditions is not a result. The same tick is 20 ms with nine hundred cubes
in the scene and 4 ms without them, on the same core in the same minute. So each entry names the
apparatus, the method and the outcome, and `bench_players --log` appends the conditions beside
every number rather than leaving them to a commit message. An entry that turned out to be
invalid stays here and says why: a run that is deleted teaches nothing twice.

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

## 2026-08-12: the simulation replays, so lockstep is not ruled out

Not a timing entry. Deterministic lockstep sends intent and nothing else, and every peer arrives
at the same world by simulating it — which only works if the same inputs give the same state bit
for bit. That is a property, not a number, and it gates a whole topology, so it was asked before
anything was built on the answer.

`bench/determinism_probe.c`, two worlds from one MJCF string, driven by an input that is a pure
function of the tick, compared with `mj_getState(..., mjSTATE_INTEGRATION)` every tick.

| shape                                      | entities | ticks | result       |
| ------------------------------------------ | -------: | ----: | ------------ |
| 900 cubes, 4 players, one process          |      912 |  1200 | identical    |
| 900 cubes, 4 players, **two processes**    |      912 |  1200 | traces agree |
| 900 cubes, 166 players, a full ward        |     1398 |   600 | identical    |
| 466 players, no cubes — no contacts at all |     1398 |   600 | identical    |
| 1400 cubes, no players — maximum contacts  |     1400 |   600 | identical    |

The state is 28063 `mjtNum` at 912 entities, 224504 bytes, and every byte matched.

**So topology 1 stays on the table.** That is the whole finding, and it is worth what it cost:
the alternative was building an intent-driven lockstep harness and discovering the answer
afterwards.

### `sim-hz` is a wire constant, not a tuning knob

The one run that differed is the one that should. The same scene at 120/20 Hz instead of 60/20
diverges from the 60/20 trace **at tick 0** — a different substep split is a different
simulation, not a worse one.

That makes the simulation rate part of the wire contract for any lockstep zone: two peers that
disagree about it do not drift apart slowly, they are in different worlds from the first tick.
It belongs pinned beside `WARD_TICK_HZ`, and a peer that cannot hit it must not be allowed to
run at a rate it can hit.

### What this does not say

- **Same binary, one machine.** This is the floor, not the question. MuJoCo promises nothing
  across platforms, `mjtNum` is a double whose rounding follows the compiler and its flags, and
  a shipped lockstep needs this answered on Linux and on the headset. `--emit` and `--compare`
  are for exactly that: run it on two machines and diff the traces.
- Sixty seconds of simulated time at most. A divergence that takes ten minutes to appear would
  not have been seen.
- No threading. MuJoCo is stepped on one thread here; `mj_step` with a thread pool reassociates
  floating-point work and is a separate question.
- Reproducible is not the same as _agreeing with another implementation_. Nothing here says a
  second physics engine, or a headset build with different flags, computes the same world.

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

## 2026-08-12: how a run is filmed, and why it is worth filming

Three bugs in the scene generator were found by looking at a picture and none by reading a
number, so rendering is part of the method rather than a way of publishing it. The numbers were
consistent, reproducible and wrong together each time: eighteen towers standing shoulder to
shoulder as one slab, a column launching itself nine metres upward, avatars spawned inside the
stack they were meant to knock over. A render showed each in seconds.

So the procedure is written down here beside the measurements it checks.

### The apparatus

- `determinism_probe --print-scene` emits the generated MJCF to stdout. Everything else depends
  on that: the scene is generated, so the only way to see what is being simulated is to ask it.
- The **Python `mujoco` package, pinned to the same 3.11.0 the repository vendors**. Same
  version, so what is rendered is what is simulated. It is a rendering dependency and is
  deliberately not in the build.
- `mujoco.Renderer` offscreen. MuJoCo's `simulate` GUI is disabled in `cmake/mujoco.cmake` and
  stays disabled — a viewer is a dependency the service does not need.
- `ffmpeg`, fed raw frames over a pipe.

### Two things that will waste an hour if forgotten

- The offscreen framebuffer defaults to 640×480 and is set **in the model**, not in the
  renderer: a `<visual><global offwidth= offheight=/></visual>` clause has to be injected before
  compiling, or `Renderer` refuses any larger size.
- The generated scene has **no lights and no colours**, because the simulation does not need
  them. Rendered as-is it is a black rectangle. Lights and `rgba` are injected into the copy
  that is filmed and never into the scene that is measured — a render that changed the model
  would be a picture of a different run.

### Frames go to the encoder, not to memory

1080×1920×3 is 6 MB a frame, so a twenty-three second clip is 4 GB of raw video. Buffering it in
a list and saving a `.npy` worked and then had to be read back; piping each frame to `ffmpeg`
as it is rendered costs nothing and bounds the memory.

### Realtime is a result, not a setting

Wall clock is measured while rendering and reported as a factor. An offline render played back
at 30 fps looks realtime whatever the simulation cost — a scene at 0.02× and a scene at 4× make
the same video. The flat field measured **4.15×** with 912 bodies and 3678 contacts; the same
run is honest to publish only because that number was taken.

### Citation travels in the file

`CITATION.cff` is read at encode time and its fields become container metadata — title, author,
ORCID, licence, version, date, and a comment naming the repository and pointing at the citation
file. A clip separated from the repository still says what it is and who made it, which a
filename does not.

Encoded as ProRes 422 HQ. It is a mezzanine format: large, meant for editing rather than
posting, and a smaller delivery encode is made from it rather than the reverse.

### What this is not

Not evidence of correctness. A render shows the shape of a scene and the shape of its failure;
it says nothing about whether the state replayed bit for bit, which is `determinism_probe`'s job
and is checked with `mj_getState` rather than with eyes.

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

## 2026-08-12: the second wall is the NIC, and it wants a different machine

The first entry already said the fan-out overtakes the physics and that egress is the limit. What
it did not say is that the two limits live on **different machines**, and that this is forced
rather than chosen once network interfaces are per-VM.

| tier | binds on | its other resource |
| --- | --- | --- |
| zone (MuJoCo) | core — largest island | egress 22.4 Mbps, never NIC-bound |
| fan-out | NIC | no physics, never core-bound |

A combined process wastes whichever resource it does not bind on. Split, each saturates the one
it is actually limited by.

The fan is worth stating as an asymmetry: a fan-out process pays **22.4 Mbps of ingress once** —
one copy of zone state — however many subscribers it serves, and everything above that is egress.
On a 1 Gbit interface at the measured 0.349 Mbps a subscriber that is 2797 subscribers, an
amplification of 43.7×. **Interest management is what makes the fan possible**: 64 of 1398
entities is 21.8×, and without it the same interface carries 130 subscribers rather than 2797.

No cascade is needed at any scale being discussed. At 22.4 Mbps per downstream a 1 Gbit zone
feeds 44 fan-out processes, about 123,000 subscribers, on a flat tier.

### Which shape of machine

Modelled, not measured — `N`-core machines at a flat $31/core/month, 1 Gbit per machine, egress
at $0.02/GB, one million players:

| | zone VMs | fan VMs | machines | cores | compute/mo |
| --- | ---: | ---: | ---: | ---: | ---: |
| all-small | 1000 | 153 | 1153 | 1153 | $35,743 |
| all-large | 63 | 233 | 296 | 4736 | $146,816 |
| **large zone, small fan** | 63 | 153 | **216** | 1161 | **$35,991** |

Neither extreme is right because the tiers want opposite shapes. Zones want **large**: islands are
independent so cores fill exactly, and — the better reason — islands on one machine coordinate
through memory rather than through the network, so a large zone machine makes many zones' worth of
seams free. Fan-out wants **small**: the interface is per-machine, so a large fan-out machine pays
for cores it cannot feed.

**Egress is 96% of the bill at that scale and is identical in every row.** Machine shape is a
3.6% cost decision and a 5× machine-count decision. At lower egress prices — committed transit,
private links — the shape choice becomes most of the bill instead, so the ratio is worth
re-deriving rather than inheriting.

### What this does not say

- Everything after the 22.4 Mbps and 0.349 Mbps figures is **arithmetic on a model**, not a
  measurement. No fan-out tier has been deployed and no machine of any shape has been rented.
- The flat-per-machine interface assumption is the whole fan-out argument. If bandwidth scales
  with machine size instead, large fan-out machines tie rather than lose, and the mixed shape's
  advantage collapses to the zone tier alone. **This is one number to check with a provider and
  it has not been checked.**
- $31/core and $0.02/GB are round numbers chosen for internal consistency, not quotes.

## 2026-08-12: which tricks survive the worst case, and why the tree carries two curves

### Yields and bounds

Sorting the optimisations by what they do in the worst case — everything moving, everyone in one
place — separates them cleanly, and the separation predicts which will disappoint:

| | kind | in the churn case |
| --- | --- | ---: |
| send only what changed | yield | **2%** |
| speculative branch-and-merge instead of locking | yield | **1.1×** |
| locked volumes | bound | holds |
| fixed-budget priority accumulator | bound | holds |
| log-depth routing overlay | bound | holds |

**Yields key off things not interacting, and the worst case is defined by interaction.** Avatars
are the reason: they are mocap, driven from outside, and a tracked headset never rests, so the
dormant set is close to empty exactly when it is most needed. A yield cannot be sized against
because its value is whatever the players happen to leave alone.

This is why the priority accumulator matters more than its compression ratio suggests. Interest
management and a fixed 64-packet budget give the same reduction on a spread-out scene; only the
second still gives it when everyone stands in one place.

### The lockstep crossover moves once reliability is priced

The second entry left lockstep open on the grounds that the simulation replays. Its bandwidth
advantage is real and smaller than it looks: a lost input in lockstep is not a glitch but a
permanent desync, so inputs need redundancy, and every peer needs every input — there is no
interest management for intent. Crossover against a fixed 64-entity slice:

| intent encoding | crossover |
| --- | ---: |
| 30 B, no redundancy | 74 players |
| 3 poses quantised, no redundancy | 37 |
| the same with 2× redundancy | **19** |

Below that lockstep wins by a wide margin, which is why the article's four-player host chose it.
A ward of 166 is not below it.

### Two space-filling curves, and why neither can be dropped

`lean-spatial-oracle`'s `PredictiveBvh.core.CurveDuality` proves this rather than asserting it,
because it came up as "surely one of them is redundant" and the answer is no:

| | Morton | Hilbert |
| --- | ---: | ---: |
| `f(a⊕b) = f(a)⊕f(b)` | **4096/4096** | 1600/4096 |
| query ranges, 3×3 windows over 16×16 | 868 | **568** |
| worst single window | 5 | **4** |

Morton is a bit permutation and therefore GF(2)-linear. A butterfly overlay is the Cayley graph of
(ℤ/2)ⁿ under XOR, so its stages are XOR by a basis vector — linearity is what makes a stage a
spatial translation rather than an arbitrary jump. Hilbert is not linear, because each level's
rotation is chosen by the prefix.

What Hilbert buys is locality **on query windows only**, and the cluster counts are the price of
doing without it.

It does not partition better, which is what this entry first claimed. Cutting the code space into
equal contiguous ranges — how `Fabric.lean` assigns entities by prefix — costs the same seams
under either curve at every zone count tried:

| zones | row-major | Morton | Hilbert |
| ---: | ---: | ---: | ---: |
| 4 | 48 | **32** | **32** |
| 16 | 240 | **96** | **96** |
| 64 | 288 | **224** | **224** |

The query metric alone is a trap, and row-major is the proof: it beats Morton on 3×3 windows
while costing 240 seams against 96. Measuring at a two-way split hides this, because every curve
gives a half-rectangle there and all three tie at 16.

Morton is not optimal among linear curves either. Exhaustively over all 720 bit permutations at
order 3, one clusters 15% better at equal seam cost — but it does not survive to order 4, where
the best that still ties on seams is only 3% better. **15% to 3% across one doubling** is why
Morton stays rather than a second addressing scheme being introduced.

Neither is the other's dual. (ℤ/2)ⁿ is Pontryagin self-dual, so Morton's structure is its own
dual — which is why a butterfly can be transposed and run in reverse at all. Hilbert has no
characters to dualise. **They are complements, not alternatives**, and carrying both is forced.

### What this does not say

- The curve figures are exact and proved by `native_decide` on 8×8 and 16×16 grids. Whether the
  ratios hold at the 30-bit codes actually used is not proved — and the one trend that was checked
  across a doubling **shrank**, so extrapolating them upward is not safe.
- **Every Hilbert figure above is for a correct Hilbert curve, and the deployed one was not.**
  See the amendment below; it was found after this section was written.
- The 1600/4096 is worth noticing: Hilbert satisfies the linearity identity for a large minority
  of pairs, so **sampling a few pairs would make it look linear**. An earlier throwaway check with
  a wrong reflection term reported 146/4000 and would have supported the same conclusion for the
  wrong reason.
- No routing overlay is built. The curve work says which code it would have to use, not that it
  is worth building — at present scales the log-depth saving is a wash.

## 2026-08-12 (amendment): the curve we were measuring was not the curve we were running

The section above compares Morton against Hilbert and reasons about which to keep. It is sound
about the two curves and was answering the wrong question, because `Shared.hilbert3D` — the
encoder every zone assignment, BVH sort key and authority lookup in this workspace goes through —
**was not a Hilbert curve.**

Walk the codes in order and every step must move exactly one cell. Measured:

| encoder | consecutive steps that are NOT face-adjacent | max step |
| --- | ---: | ---: |
| `Shared.hilbert3D`, as deployed | **87.5%** (3583/4095 at 16³, 229375/262143 at 64³) | 19 |
| Skilling 2004, correct | **0%** | 1 |
| Morton | 50% | 63 |

Confirmed three ways that share no code: a Lean adjacency test here, and two independent C ports
written separately against the same file. All three agree on 87.5% to the digit.

**It had Morton's locality and none of Morton's speed** — 5× the encode cost, worse zone
connectivity (912 disconnected components for 112 zones, against Morton's 160 and a correct
Hilbert's 112), and no better mean locality.

### Why it survived

Everything that was being checked passed. It is a clean bijection over 1024³ → [0, 2³⁰). The
round trip closes. The docstring cites a paper. **None of those distinguish a Hilbert curve from
any other bijection**, and that is the whole lesson: the tests asserted consequences of the
property instead of the property.

Two deviations from Skilling 2004, both needed. The main loop omitted `i = 0`, where the exchange
branch is a no-op — which is why it looks droppable — but the invert branch is not; and it ran
the remaining pairs backwards, which matters because every step mutates `X[0]`. The interleave
then emitted `z` as the most significant bit of each group where `x` belongs. Fixing only the
first leaves 87.5% unchanged.

Fixed in `lean-shared-core#2`, with the defining property as a build-time gate, plus the
worst-case run-extent bound — a run of L consecutive codes fits in a box of side `2·L^(1/3) − 1`,
which is the actual reason to pay for this curve, since Morton has no such bound at any L.

### What this costs, and what it does not say

- **Every code value changes.** Any persisted Hilbert code, or a zone assignment derived from
  one, is invalidated.
- The forward fix and the matching inverse in `lean-spatial-oracle`'s `CodeGen.lean` **must land
  together**; either alone leaves the round trip closing on the wrong cell.
- `thirdparty/spatial-oracle` here is a vendored copy still carrying the old inverse. It is not
  re-vendored yet, because the rule is to fix upstream first and vendor a merged fix.
- **No timing in this logbook is invalidated.** The bug is a locality defect, not a cost one, and
  no entry above measured broadphase or zone assignment — the benchmarks use ghost-AABB overlap
  with a counting sink. What is invalidated is any *locality* claim made about production.
- The head-to-head numbers behind the amendment were produced by two agents in a scratch
  directory, not by anything in this tree, and are not reproducible from this repository.

## Every run, as it was logged

`bench_players --log docs/logbook/one_core.md` appends here. The rows below are the raw
conditions and outcomes; the sections above are what they mean. Rows are never edited or
removed — a measurement that turned out to be wrong gets a section saying so.

| when             | run   | players | cubes | per player | entities | sim Hz | pub Hz | interest m | ticks | median ms | worst ms | simulate | encode | fanout | contacts |  sent |   bytes |
| ---------------- | ----- | ------: | ----: | ---------: | -------: | -----: | -----: | ---------: | ----: | --------: | -------: | -------: | -----: | -----: | -------: | ----: | ------: |
| 2026-08-12 07:09 | fixed |       4 |   900 |          3 |      912 |     60 |     20 |       10.0 |    60 |     21.12 |    58.47 |    20.83 |   0.04 |   0.01 |     3600 |   256 |   25600 |
| 2026-08-12 07:09 | fixed |     166 |   900 |          3 |     1398 |     60 |     20 |       10.0 |    60 |     20.53 |    26.96 |    19.59 |   0.06 |   0.45 |     3600 | 10624 | 1062400 |
| 2026-08-12 07:09 | fixed |     466 |     0 |          3 |     1398 |     60 |     20 |       10.0 |    60 |      3.98 |     5.63 |     1.04 |   0.04 |   3.02 |        0 | 29780 | 2978000 |
