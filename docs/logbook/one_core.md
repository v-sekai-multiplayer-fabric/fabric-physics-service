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

## 2026-08-12: a stack is a different problem, and the field was flattering us

The entry above ends by saying the 900 cubes are a settled grid rather than the article's
twenty-metre stacks, and that a stack would cost more. It costs enormously more, and the shape
of the cost is not what "more" suggests.

`bench_players --stack 50` builds eighteen towers fifty cubes high — twenty metres, which is the
article's own figure. Same 900 cubes, same four players, same everything else.

| ticks run | median | worst | simulate | contacts at the end |
| ---: | ---: | ---: | ---: | ---: |
| 12 | 20.72 ms | 51.26 ms | 23.55 | 3356 |
| 40 | 17.80 ms | 226.56 ms | 45.90 | 718 |
| 100 | **89.61 ms** | **1840.56 ms** | 320.19 | 3311 |

**A standing tower is cheap and a falling one is not.** At twelve ticks the stacked scene is
indistinguishable from the flat field — 20.7 ms against 20.5 — because nothing has moved yet.
By a hundred ticks the median is 89.61 ms, over the 50 ms budget on its own, and one tick took
**1840.56 ms**. That is thirty-seven times the budget in a single tick.

The first sign of this was a run that looked hung. The determinism probe at `--stack 50` was
killed after nine minutes; it was not stuck, it was simulating two collapsing worlds at seconds
per tick. A measurement that looks like a hang is worth a second look before it is called one.

### What it does to the number above

**The 166-player result is a number about an empty room.** It was measured on a settled grid,
and a settled grid is what a zone looks like before anybody plays in it. The moment a player
knocks a tower over, this core misses its tick by a factor of thirty-seven, and no topology
choice changes that: lockstep, authority and relay all have to integrate the same collapse, and
in lockstep *every peer* does.

The entry stands rather than being edited, because it is true about what it measured. It is the
scope that was wrong, not the arithmetic.

### What it means for the comparison

Contacts are not a property of the scene, they are a property of the moment. 3356, then 718,
then 3311 as the towers fall and settle — so a single tick count is not a benchmark of anything
and the three topologies must be compared over the same interval of the same collapse, not at
whatever tick each run happened to stop.

It also makes the interesting question a different one. "How many players fit" assumed the cost
was steady. What a zone actually needs to survive is the worst tick, and the worst tick here is
two seconds — so the next thing worth measuring is not a bigger ceiling but what a zone does
when one tick takes two seconds, which every topology must answer and none of them answers by
being faster.

## Every run, as it was logged

`bench_players --log docs/logbook/one_core.md` appends here. The rows below are the raw
conditions and outcomes; the sections above are what they mean. Rows are never edited or
removed — a measurement that turned out to be wrong gets a section saying so.

| when             | run   | players | cubes | per player | entities | sim Hz | pub Hz | interest m | ticks | median ms | worst ms | simulate | encode | fanout | contacts |  sent |   bytes |
| ---------------- | ----- | ------: | ----: | ---------: | -------: | -----: | -----: | ---------: | ----: | --------: | -------: | -------: | -----: | -----: | -------: | ----: | ------: |
| 2026-08-12 07:09 | fixed |       4 |   900 |          3 |      912 |     60 |     20 |       10.0 |    60 |     21.12 |    58.47 |    20.83 |   0.04 |   0.01 |     3600 |   256 |   25600 |
| 2026-08-12 07:09 | fixed |     166 |   900 |          3 |     1398 |     60 |     20 |       10.0 |    60 |     20.53 |    26.96 |    19.59 |   0.06 |   0.45 |     3600 | 10624 | 1062400 |
| 2026-08-12 07:09 | fixed |     466 |     0 |          3 |     1398 |     60 |     20 |       10.0 |    60 |      3.98 |     5.63 |     1.04 |   0.04 |   3.02 |        0 | 29780 | 2978000 |
