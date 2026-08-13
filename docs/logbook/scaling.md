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
