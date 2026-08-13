# What a demo has to show

Every improvised MuJoCo demo in `bench/` and `tools/` answers to this list. It is not a
wishlist — it is fifteen answers to "pick two features for a massive multiplayer game,
a primary and a fallback", collected from the community and shared 2026-08-12. Respondents are anonymised to single
letters; the mapping is not recorded here.

A demo that does not touch one of these is measuring something nobody asked for. The
Jenga work is the cautionary case: technically clean, and it modelled a workload no
game in this category has.

## The wants, tallied

| theme | answers | who |
| --- | ---: | --- |
| **build / create / UGC** | **8 / 14** | A, B, D, G, I, L, M, O |
| social / presence | 5 | E, J, M, L, F |
| shared persistence | 5 | B, D, L, O, I |
| specific content | 3 | N (fishing), K (spaceship), C (horde) |
| scale | 2 | B (single shard), C (1000+ enemies) |

Verbatim, where it matters:

- **B**, which is this project's own answer — *primary: build everything in world using a
  meshing pen. fallback: single shard.*
- **O** — *letting everything you affect/build in the game be seen by other players.
  It's one thing I don't like in vrchat that there are still worlds with items localized.*
- **M** — *virtualized reality is not a headset strapped to the head, but play-spaces to
  socialize.*
- **C** — *horde fighting, 1000+ enemies all grouping and following you.*
- **I** — *user created content unlocks most things.*

O names the failure this whole service exists to prevent: things you build that
other people cannot see. Server-authoritative physics is the answer to that sentence.

## What each want costs, measured

| want | measured cost | verdict |
| --- | --- | --- |
| 1000-enemy horde, simple driven agents | 1 zone, 0.5% of tick | free |
| 1000-enemy horde, full driven humanoids | 40 zones, 0.6% each | free |
| 25 ragdolls at once | 33.7 ms | **67% of tick** |
| building, 200 pieces being placed (coupled) | 9.2 ms | fits |
| building, 5000 pieces placed and still dynamic | 107 ms | **over budget** |
| building, 5000 pieces frozen to static | ~0 ms | free |
| single shard, any population | 0.322 ms per zone | flat |

**Two things are expensive and they share one fix.** Built structures left dynamic, and
too many simultaneous ragdolls. Freeze what is not moving — measured at 328x on a flat
field at 100% frozen, 7.85x at 90% — and both go away.

## Rules a demo should respect

1. **Show something built, and show it persisting.** Eight of fourteen asked for this.
   A demo of loose props falling over is not it.
2. **Show it from more than one viewer's authority.** O's complaint is about
   locality, so a demo where the built thing exists in one place only misses the point.
3. **Prefer many cheap agents over few expensive ones.** A thousand driven agents cost
   less than twenty-five ragdolls. That is counter-intuitive and worth showing.
4. **Freeze on settle, visibly.** Colour it. The mechanism that makes the game possible
   should be legible in the clip.
5. **Do not model coupled towers unless the point IS coupling.** See the Jenga entries in
   `docs/logbook/` for what that measures and what it does not.

## The tooling that exists

- `tools/record_jenga.py`, `tools/record_jenga_tall.py` — 1920x1080 ProRes mezzanine,
  zone colour-coding, citation in container metadata, realtime factor reported.
  `--delivery` makes the H.264 from it.
- `bench/human_scale.py` — millimetres in ordinary objects, because past a centimetre a
  number stops carrying size.
- `bench/mmog_archetypes.py` — every shape in the category costed against one zone.
- `bench/humanoid_cost.py` — a Godot humanoid driven versus simulated, 103x apart.

CASSIE is the meshing pen: `V-Sekai.cassie` (Godot port), `cassie` (original),
`cassie-data` (recorded sketches), and `fabric-godot-core/modules/cassie`, which already
carries a Lean formalisation.
