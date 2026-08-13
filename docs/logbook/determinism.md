# Determinism logbook

Whether the simulation replays bit for bit, which is a property rather than a number and gates a whole topology. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

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
