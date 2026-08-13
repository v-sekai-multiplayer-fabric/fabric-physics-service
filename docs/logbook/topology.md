# Topology logbook

Lockstep, client-server, distributed authority and rollback: what is measured, what is derived, what is a theorem, and what nobody has looked at. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

## 2026-08-12: what is settled about the topology, and what is not

Written because the question came back and nobody could answer it from the record. That is the
finding: **no topology has been chosen, and none of the entries above says so.** A recommendation
was made in conversation and a recommendation is not a decision, which is exactly the difference
this file exists to keep.

The comparison the plan called for -- `bench_players --topology authority|lockstep|relay` -- was
never built. The plan said plainly that it "does not pick a topology, it produces the table that
a decision can be made from". The table does not exist.

### What is measured

| | |
| --- | ---: |
| client-server, per subscriber, zstd -3 worst case | **0.349 Mbps** |
| the same, uncompressed | 1.02 Mbps |
| one zone's full state, one copy | 22.4 Mbps |
| MuJoCo replays bit-identically | yes, nine shapes |

Client-server's number is **O(1) in population**: `MAX_SLICE_ENTITIES` caps a slice at 64, so a
subscriber costs the same at four players and at four hundred. That is the whole of its case and
it is the only one of the three with a measured figure.

### What is derived, not measured

Lockstep is **O(N) per peer and cannot be interest-filtered** -- every peer needs every input, or
it desyncs. A lost input is not a glitch but a permanent divergence, so inputs need redundancy,
which is not free. Crossover against a 64-entity slice:

| intent encoding | crossover |
| --- | ---: |
| 30 B, no redundancy | 74 players |
| 3 poses quantised, no redundancy | 37 |
| the same with 2x redundancy | **19** |

Below that lockstep wins by a wide margin, which is why the article's four-player host chose it.
A ward of 166 is not below it.

Distributed authority was estimated at 0.402 Mbps a subscriber. **That number is a guess** -- slice
cost plus an assumed authority overhead -- and nothing has measured it.

### What is a theorem

Attiya & Welch (1994): where message delay uncertainty is `u`, no algorithm implements
linearizability with writes faster than **u/2**. A distributed-authority handoff is a linearizable
write, so it pays that floor; a single authority has no seam and pays nothing. At a 50 ms tick
with 20 ms of jitter that is 10 ms of the budget gone before any implementation exists.

This is a stronger argument against topology 3 than the bandwidth estimate, because it is not an
estimate.

### The option nobody evaluated

**Deterministic rollback.** Glenn's three are lockstep, client-server and distributed authority,
and rollback was folded into the first without being examined. It is a different animal: same
input-only bandwidth, so it inherits lockstep's O(N) unfilterable problem unchanged -- but it does
**not** wait for the slowest peer. It predicts and resimulates on correction.

That matters because O(N) bandwidth was the whole case against lockstep, and it survives; while
the latency case, which was lockstep's other problem, does not apply at all. Stage 0 already
proved the simulation replays bit-identically, which is rollback's precondition. So it is the one
option that is neither ruled out nor examined.

### Rollback, and the version that escapes O(N)

Rollback inherits lockstep's bandwidth problem: every peer needs every input, because a distant
player's action propagates. The escape is to bound propagation -- interest-filter the inputs
through a relay, so a peer receives only the inputs that could reach it inside the rollback
window.

**Sizing the intent is the whole answer, and the first attempt got it wrong.** An intent here is
three tracked poses and nothing else: quantised i16 delta position (6 B) and swing-twist i16
rotation (6 B) is 12 B an entity, so 37 B an avatar. No velocity, no class, no sub-index -- those
are state, not input. The first model in this entry used 60 B doubled to 120, which is above the
104 B break-even, and concluded rollback did not pay. That conclusion was an artefact of the
estimate.

| players | lockstep | relayed rollback | client-server |
| ---: | ---: | ---: | ---: |
| 16 | 0.178 | **0.178** | 0.349 |
| 64 | 0.746 | **0.249** | 0.349 |
| 166 | 1.954 | **0.249** | 0.349 |
| 1000 | 11.828 | **0.249** | 0.349 |

At 37 B doubled for redundancy, relayed rollback saturates at **0.249 Mbps -- 1.41x cheaper than
client-server**, at any population. Delta-coding the redundancy instead of duplicating it takes it
to 0.188 Mbps and 1.86x. It is O(1) and it is the cheapest of the three on the wire.

The break-even is 104 B an intent at an interest set of 21. Anything under that and inputs beat
state, which is the shape one expects: an input is what a pose IS, and a state packet is that plus
everything derived from it.

**The latency is the same 2d**, client to relay to client -- identical to input up and state down.
So against client-server this is not a latency-for-bandwidth trade at all. It is cheaper on the
wire at the same delay.

Peer-to-peer rollback keeps 1d latency, which is real and is why fighting games use it, and keeps
the O(N) bandwidth, which is what filtering trades away.

**So the deciding cost is not bandwidth.** It is that every client simulates the world, and the
client is a headset. Client-server's case was never really the wire; it was that one machine
simulates and the others draw. That is the comparison worth measuring, and nothing here has.
### Can the crossover be pushed to a thousand players?

Only for the UNFILTERED form, and it is the wrong question -- but the arithmetic is worth having
because it says how much room is left.

Unfiltered lockstep is O(N), so the crossover is wherever `(n-1) x intent` meets the slice. To
put it at a given player count the intent has to fit:

| crossover at | vs 0.349 Mbps (today) | vs 0.149 Mbps (predicted + quantised) |
| ---: | ---: | ---: |
| 19 | 121.35 B | 51.91 B |
| 166 | 13.24 B | 5.66 B |
| 547 | 4.00 B | 1.71 B |
| **1000** | **2.19 B** | **0.94 B** |
| 2000 | 1.09 B | 0.47 B |

Three poses is eighteen degrees of freedom, so a thousand players unfiltered needs **0.97 bits a
degree of freedom** -- and against a client-server that has had its own prediction work done,
**0.42 bits**. Measured residuals today are 3.53 B quantised position and 4.95 B delta rotation an
entity, so 25.4 B an avatar: a **12x further compression** to reach the first column, and 27x to
reach the second.

Under a bit per degree of freedom is not obviously impossible for band-limited human motion at
20 Hz, but nothing here is close to it, and the client-server side is a moving target -- every
improvement there raises the bar rather than lowering it.

**The question is wrong because the filtered form does not have a crossover.** Interest-filtering
removes the N dependence entirely: relayed rollback saturates at 0.249 Mbps and stays there at a
thousand players, at ten thousand, at any number. It is already cheaper than client-server
everywhere. There is nothing to push.

What that buys is worth stating plainly: the reason to chase a smaller intent is not to reach a
crossover, it is that a smaller intent makes the filtered form cheaper still. 37 B doubled is
0.249 Mbps; delta-coded at 1.5x it is 0.188. The compression work pays either way and the
crossover arithmetic is a distraction from it.

### The clustered forms, and where they stop being different things

Clustered client-server is the model already in [scaling.md](scaling.md): zone cores that
simulate, a stateless fan-out tier that distributes, seams carried as ghosts. The client draws
and interpolates and simulates nothing.

Clustered rollback is the interesting one, because clustering changes what a client has to
simulate. In full lockstep every peer simulates the whole world. Filtered by interest, a peer
only needs the 64 entities in its slice -- **21x less than the 1398 in the zone**. Costed against
the measured island law:

| | server, per tick | client, per tick | per-client |
| --- | ---: | ---: | ---: |
| clustered client-server | 19.6 ms | ~0, interpolation only | 0.349 Mbps |
| clustered rollback, K=3 | 0, relay only | **2.96 ms x rollback depth** | 0.249 Mbps |

Rollback depth is what decides it. One tick of correction is 2.96 ms; four is 11.8; eight is
23.6. A 72 Hz frame is 13.9 ms total for render and logic, and a 20 Hz physics tick spans about
three and a half of them, so five milliseconds of spare CPU per tick is a generous read of what a
headset has.

**So the 0.1 Mbps saving is bought with three to twenty-four milliseconds on the device least
able to pay it.** Bandwidth was never the reason to prefer client-server; this is.

### Clustering rollback turns it into distributed authority

There is a structural problem underneath the arithmetic, and it is worth stating because it is
easy to model past.

Rollback works because every peer simulating the same inputs reaches the same state. A peer that
simulates only its interest set is not simulating the same world as a peer whose set is different
-- they agree in the overlap and diverge outside it. So a clustered rollback needs a rule for what
happens at the boundary between two peers' sets: who is authoritative for a body both can see,
and what happens when it crosses.

That rule is distributed authority. Clustered rollback is not a fourth topology sitting beside
the other three; it is topology 3 with prediction bolted on, and it inherits topology 3's floor:
a linearizable handoff cannot beat **u/2** (Attiya & Welch 1994), which at 20 ms of jitter is
10 ms of a 50 ms tick.

Unclustered rollback avoids this entirely -- everyone simulates everything, so there are no
boundaries -- and pays O(N) bandwidth for the privilege. The bandwidth and the seam are the same
trade seen from two ends: filtering is what creates the boundary that then needs a protocol.

**That is the finding.** The three topologies are not three points; lockstep and distributed
authority are the ends of one axis, and where you sit on it is set by how much you filter.
Client-server is off that axis, which is why it has neither problem: one simulator, no seam, no
O(N).

### What this does not say

- **All of the above is arithmetic on a model.** No rollback harness exists here. The 60 B intent
  and the 2x redundancy are estimates, and the interest cap is read across from the slice.
- Rollback's real cost is not in this table. Resimulating N ticks on every correction is CPU on
  the client, and the client is a headset. Nothing here measures that, and it is the number that
  would decide it.
- The misprediction rate is unmeasured, and it sets both the resimulation cost and how often a
  player sees a snap. A physics sandbox where players throw things at each other is a worse case
  for prediction than a fighting game, which is where rollback's reputation comes from.

### What this does not say

- **No decision is recorded here, deliberately.** Client-server has the only measured number and
  the only population-independent cost, and that is a reason rather than a choice. Writing a
  choice nobody made would be worse than leaving the question open.
- Distributed authority's 0.402 Mbps is arithmetic on an assumption. Treat it as unmeasured.
- Rollback has no numbers at all.
- The three-topology bench is still unwritten, and it is the thing that would replace most of the
  above with measurements.
