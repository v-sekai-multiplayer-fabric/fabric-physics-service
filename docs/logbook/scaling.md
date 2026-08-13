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
