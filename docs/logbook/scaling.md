# Scaling logbook

What binds as players are added, and on which machine. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

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

## 2026-08-12: what happens if bandwidth and CPU both get cheaper

Nothing. That is the finding, and it is worth the entry because it says which constraints are
real.

Sweeping both together -- per-subscriber bandwidth divided by `f`, per-body and per-player
simulate cost divided by `f` -- and asking what binds:

| improvement | Mbps a subscriber | players a zone (CPU) | subscribers a NIC | what binds |
| ---: | ---: | ---: | ---: | --- |
| 1x | 0.349 | 1,544 | 2,735 | join reply cap |
| 4x | 0.087 | 62,082 | 10,940 | join reply cap |
| 16x | 0.022 | 304,234 | 43,763 | join reply cap |
| 1000x | 0.0003 | 20,160,736 | 2,735,243 | join reply cap |

The binding constraint never changes, because **none of the walls are made of hardware**:

- `WARD_AUTHORITY` 1400 gives `(1400 - 900) / 3` = **166 players a zone**. It is a bare
  constant, and `AbyssalSLA.lean` -- where it comes from -- derives nothing: it states 1800 with
  no proof behind it, for a jellyfish demo at 56 entities a player and 16 players a zone.
- `WARD_REPLY_MAX` caps a full snapshot at 2621 entities, so **873 players** before a join reply
  cannot be sent whole. `CLAUDE.md` already says the answer is a chunked reply; nothing
  implements it.
- Attiya & Welch's `u/2` is **10 ms of a 50 ms tick** at 20 ms of jitter, and a faster machine
  does not move a lower bound on message delay.

So the compression work and the solver work both pay a bill and neither buys a player. At a
thousand players egress is 349 Mbps -- one NIC -- and the tick has room; what stops the zone is a
number somebody wrote down for a different game.

**This is the argument for re-deriving the budget rather than optimising underneath it.** Two
constants and a chunking routine are worth more than any factor of sixteen on the wire.

### What this does not say

- The CPU column is optimistic in the way [one_core.md](one_core.md) warns about: it prices a
  player at 2.23 us, which is the *contactless* cost measured with no cubes in the scene. Players
  in a dense zone contact things. The column should be read as an upper bound, and the argument
  does not need it -- the entity budget binds an order of magnitude below even this.
- Nothing here measures a zone at 166 players with players actually touching the cubes. That
  measurement is what would put a real number in the CPU column.
- The sweep divides both costs by the same factor, which is a modelling convenience. Compression
  and solver speed do not improve together in practice.

## 2026-08-12: a zone seam costs motion, not seams

A Jenga tower is one contact island top to bottom, so it is the sharpest test of what a
boundary does to something spanning it. `bench/jenga_torture.py` cuts one every awkward way:
horizontal slabs from two up to eighteen, a vertical plane that slices through levels rather
than between them, and both crossed into a 3D grid. Ghosts are the naive kind -- a block
outside a zone is a static geom at its start position, no velocity, no handoff.

| partition | settled tower | collapsing tower |
| --- | ---: | ---: |
| 2 slabs, 1 seam | 2.27 mm | 14.32 mm |
| 6 slabs, 5 seams | 2.90 mm | 14.47 mm |
| 18 slabs, 17 seams | 2.97 mm | 14.49 mm |
| vertical plane through levels | 2.97 mm | 14.48 mm |
| 3D grid, 6 slabs x 2 halves | 2.97 mm | 14.48 mm |

Maximum drift, against a whole-tower ground truth. A block is 15 mm tall.

**Seam count does not matter.** Going from one seam to seventeen costs 0.17 mm on a collapsing
tower and 0.70 mm on a settled one. Cutting through a contact plane rather than between two
costs nothing measurable. A full 3D partition is indistinguishable from a single cut.

The reason is what a frozen ghost is wrong by: how far that block would have moved. That does
not depend on how many zones are looking at it, so the error saturates immediately and stays
there. **What costs is ghosting something that is moving**, which is why the settled tower is
six times better than the collapsing one at every partition.

So the instinct to cut as little as possible is wrong. **Fine spatial partitioning is nearly
free, and the fix for seam error is ghost velocities rather than fewer seams.** That is worth
knowing before designing a zone layout around minimising boundaries.

This is the third measurement today landing on the same rule. Sleep is worth 1.0x while things
move and up to 1115x once they settle. Interest filtering caps cost when players are spread and
degenerates when they crowd. And now a seam costs in proportion to the motion crossing it.
**Settled things partition freely; moving things do not partition at all.**

### What this does not say

- The ghost model is the naive floor: frozen, no velocity, no handoff. A real implementation
  carrying ghost velocities should beat every number here, and none of them is a limit.
- Each configuration ran once. The settled column is stable to within a tenth of a millimetre
  across partitions, which is reassuring; the collapsing column is one sample of a chaotic
  process and should be read as indicative.
- 500 steps at a 2 ms substep is one second of simulated time. A longer collapse would drift
  further and nothing here says how much.
- Both scripts now flag divergence above one full block. `jenga_multizone.py` originally used
  half a block, so the same 14.32 mm read DIVERGED there and agreed here. One number, two
  verdicts, which is exactly the kind of thing this logbook exists to catch.

## 2026-08-12: a tall tower, and a pull done as an avatar motion

Everything above deleted a block to make a tower fall, which is not what a player does. A
player's hands are mocap bodies -- no degrees of freedom, position written every tick, still
colliding -- so `bench/jenga_tall.py` makes the block being removed a mocap body and slides it
out over time. The tower has to survive being handled rather than survive a deletion.

### How tall it stands, and what it costs

| levels | blocks | height | verdict | ms/tick |
| ---: | ---: | ---: | --- | ---: |
| 18 | 54 | 26 cm | stands | 5.70 |
| 30 | 90 | 44 cm | **stands** | 55.44 |
| 45 | 135 | 67 cm | falls | 88.31 |
| 60 | 180 | 89 cm | falls | 141.47 |

Thirty levels is about the limit, and it already costs 55 ms of a 50 ms tick while it is
settling. Timed over the settling, so this is the expensive window rather than the steady state
-- the same trap the sleep entry documents, and worth naming again here because the number looks
alarming out of context.

### The pull matters as much as the block

At thirty levels, only one extraction leaves it standing:

| speed | level 6 (low) | level 15 (middle) |
| --- | --- | --- |
| 2 cm/s | collapses | **stands** |
| 10 cm/s | collapses | collapses |
| 50 cm/s | collapses | collapses |

A slow pull at mid-height survives; brisk at the same block does not. **The speed of the avatar
motion is as much of a variable as which block is chosen**, which nothing in this workspace had
considered -- an avatar's hands move at whatever speed a person moves them, and that is an input
the zone does not control.

### Across zones, a collapsing tall tower does not merely drift

| zones | seams | mean drift | max drift |
| ---: | ---: | ---: | ---: |
| 2 | 1 | 272.32 mm | 648.96 mm |
| 3 | 2 | 273.40 mm | 650.79 mm |
| 5 | 4 | 276.57 mm | 651.13 mm |
| 10 | 9 | 284.78 mm | 651.27 mm |

651 mm on a 44 cm tower. The zones are not disagreeing about where a block is, they are
simulating **different collapses**. And the seam-count rule holds even here -- one seam to nine
costs 13 mm out of 651 -- which confirms it at a scale where the error is total rather than
marginal.

So the earlier finding stands and sharpens: seam count is free, motion is not, and past some
amount of motion the boundary stops being an approximation and becomes a different simulation.

### What this does not say

- Ghosts are still frozen, with no velocity and no handoff. This is the floor.
- One run per configuration. A collapse is chaotic and the 651 mm should be read as "total",
  not as a repeatable quantity.
- The 55 ms at thirty levels is measured across settling, not at rest.
- `tools/record_jenga_tall.py` films it at 1920x1080, five colour-coded zones, mocap hand in
  red. It renders at **0.99x realtime**, which is marginally under the bar the video work set
  and is stated rather than rounded up.

## 2026-08-12: the category, costed -- and Jenga was the wrong model

Almost nothing in this category does rigid-body contact. An avatar is driven from outside, the
world it stands on is static geometry that never enters the broadphase, and props are decoration.
So a tower of coupled blocks measures a workload no shipped game in the category has, and the
Jenga entries above should be read as a stress test rather than as a model of anything.

`bench/mmog_archetypes.py` costs the shapes the category does have, at **56 entities a player** --
a full game avatar with rig, held items and effects, not the three-entity head-and-hands
abstraction the physics bench uses.

| shape | players | entities | zones | ms/zone | of tick |
| --- | ---: | ---: | ---: | ---: | ---: |
| instanced dungeon | 5 | 300 | 1 | 0.6 | 1% |
| instanced raid | 24 | 1,384 | **1** | 1.9 | 4% |
| open-world questing zone | 150 | 8,460 | 7 | 1.1 | 2% |
| capital city / social hub | 400 | 22,400 | 16 | 1.0 | 2% |
| large-scale siege | 400 | 22,500 | 17 | 1.1 | 2% |
| fleet battle | 800 | 45,000 | 33 | 1.1 | 2% |
| battle royale | 100 | 5,900 | 5 | 2.1 | 4% |
| VR social, crowded | 200 | 11,350 | 9 | 1.3 | 3% |
| survival, placed structures | 80 | 4,930 | 4 | 5.7 | 11% |
| physics sandbox, mid-build | 60 | 4,360 | 4 | 10.9 | 22% |

**Every shape fits once zoned, and the tick is never the constraint** -- the busiest zone in the
whole category is 22%, and that is the only one with coupled bodies in it.

A twenty-four player raid is 1384 of 1400 entities: **exactly one zone**. Whether or not anyone
intended it, the constant is sized for instanced content.

Everything else is a zone-count question rather than a performance one, and that works because
**the things being split do not couple**. Avatars are driven from outside and do not push each
other; ships in a fleet pass at range. Splitting either across a boundary costs a ghost and
nothing more, which is the same reason a stack of blocks cannot be split at all.

### Millimetres in things you can picture

`bench/human_scale.py` annotates every drift figure, because past a centimetre the number stops
carrying any sense of size:

| drift | | what it was |
| ---: | --- | --- |
| 2.97 mm | a grain of rice | settled tower, any partition |
| 14.48 mm | **a Jenga block is this tall** | collapsing tower, any partition |
| 534.60 mm | a pillow | coupled towers across a seam |
| 651.27 mm | a bicycle wheel | tall tower collapsing, 9 seams |
| 861.36 mm | a kitchen counter is this high | free bodies across a seam |

The 14.48 mm row is the useful one: two zones disagreeing about a block by exactly one block.

### What this does not say

- The whole table is arithmetic on per-entity costs measured elsewhere, not a measured zone.
- **56 entities a player is a stated figure, not a measured one.** Nothing here has measured what
  a 56-entity avatar costs, and if those entities are articulated rather than driven from
  outside, every row moves.
- The archetype compositions are estimates of what these games contain. They are the shape of the
  category as understood, not a survey.
