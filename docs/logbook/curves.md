# Space-filling curve logbook

Which curve orders the tree, why the tree carries two, and the day the deployed one turned out not to be the curve it was named after. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

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
