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

## Every run, as it was logged

`bench_players --log docs/logbook/one_core.md` appends here. The rows below are the raw
conditions and outcomes; the sections above are what they mean. Rows are never edited or
removed — a measurement that turned out to be wrong gets a section saying so.

| when             | run   | players | cubes | per player | entities | sim Hz | pub Hz | interest m | ticks | median ms | worst ms | simulate | encode | fanout | contacts |  sent |   bytes |
| ---------------- | ----- | ------: | ----: | ---------: | -------: | -----: | -----: | ---------: | ----: | --------: | -------: | -------: | -----: | -----: | -------: | ----: | ------: |
| 2026-08-12 07:09 | fixed |       4 |   900 |          3 |      912 |     60 |     20 |       10.0 |    60 |     21.12 |    58.47 |    20.83 |   0.04 |   0.01 |     3600 |   256 |   25600 |
| 2026-08-12 07:09 | fixed |     166 |   900 |          3 |     1398 |     60 |     20 |       10.0 |    60 |     20.53 |    26.96 |    19.59 |   0.06 |   0.45 |     3600 | 10624 | 1062400 |
| 2026-08-12 07:09 | fixed |     466 |     0 |          3 |     1398 |     60 |     20 |       10.0 |    60 |      3.98 |     5.63 |     1.04 |   0.04 |   3.02 |        0 | 29780 | 2978000 |
