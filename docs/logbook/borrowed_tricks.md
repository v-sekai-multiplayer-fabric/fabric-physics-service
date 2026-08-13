# Borrowed tricks logbook

Techniques from outside this workspace, reviewed against the walls it has actually measured
rather than against the walls it talks about. An entry that turned out to be invalid stays and
says why: a review that is deleted teaches nothing twice.

## 2026-08-12: LightSpeed's XPR model, read for loopholes

Source: *XPR Model — building a truly scalable virtual world*, LightSpeed Studios, GDC 2023.
Expand, Propagate, Reduce, with Extract and Update added in practice. The framing is quantum —
superposition, decoherence, light cones — and the framing is not the interesting part. What is
interesting is which of its mechanisms attack walls this workspace has measured.

Reviewed twice. The first reading dismissed the central mechanism for the wrong reason and
confused two things that are not the same, so both corrections are recorded below.

### The reduce operator is weighted, which makes it an authority mechanism

Page 29 is not a plain average:

    d_dest = (d_dest·w_dest + d_src·w_src) / (w_dest + w_src)
    w_dest = w_dest + w_src

A confidence-weighted merge with accumulating weight. Weight can carry authority: the owner's
branch heavy, a remote speculative branch light, and the reduce resolves toward the
authoritative answer **without anyone taking a lock**.

That matters because of the one hard result in the topology work. Attiya & Welch bound
*linearizable* objects at `u/2`, and superposition does not implement one — both branches
proceed and reconcile afterwards. It is not a way of beating the theorem; it is a way of not
being covered by it. Cost moves from every interaction to only the conflicting ones.

**The first reading of this file filed superposition as a yield** — something that saves nothing
when everything conflicts, which is true and is not the point. Its value is not compression. It
is the only mechanism reviewed here that touches the `u/2` floor at all.

### Bubbles are spatial, islands are not, and the difference is the whole review

An island is a set of bodies touching each other. It cannot be split across cores, because the
coupling is the thing being solved. That much this workspace had right.

A bubble is a *spatial* cell. Broadphase inside one is independent of broadphase inside another,
so bubbles parallelise **collision detection** — which is where the time actually goes, per the
sleep result in [one_core.md](one_core.md). Bounding islands bounds the solve, and the solve was
never the cost.

**The first reading conflated the two** and concluded that bounding island size lets a zone use
as many cores as it has. That is wrong twice over: the per-island serial ceiling is permanent
whatever you do, and the parallelism that would help is spatial rather than contact-graph.

The catch for this workspace specifically: MuJoCo's broadphase is global over one `mjModel`, so
per-bubble parallelism means a model per bubble, which puts the seams back. Right architecture,
not free in this engine.

### Two capabilities we do not have at all

Page 39 lists **long-distance effects** and **disproportionate objects** as advantages. Both are
things prefix-partitioned zones handle badly: an explosion reaching past a cell, or a body larger
than one. `Fabric.lean` assigns an entity to a zone by the Morton prefix of its centre, which
assumes the entity fits. Nothing here covers either case.

Page 38 adds variable-length spacetime IDs that **preserve parent-child relations**, so splitting
a region does not invalidate existing references. `HilbertSpan` prefixes renumber on a split. No
equivalent here.

### A decomposition question worth stealing

Page 36 asks of a non-linear interaction: **"can it be a post event?"** — can it be applied
afterwards rather than inside the loop. That is how work leaves the critical path without being
made cheaper, and it is a question worth asking of anything that looks unsplittable.

### Where all of it breaks here

**Not memory, which the first version of this entry got wrong.** Superposition is k copies of an entity, and this was recorded as expensive because the entity budget is the binding constraint. That confuses two things. `WARD_ENTITIES` binds on simulation cost and on the size of a reply, not on memory: 1800 entities at 100 bytes is 180 KB, and a server holding four copies of that would not notice.

The asymmetry is the point. **A server has RAM precisely because it is not rendering** -- no textures, no meshes, no render targets, none of what fills a client's memory -- and a headset has none spare for the same reason. In a client-server design the clients never hold branches at all, because they do not simulate. So the machine that genuinely cannot spare memory is the one place this mechanism never reaches.

The real cost is **k times the compute on contested entities**, and the deck creates copies only for out-reaching interactions rather than for everything -- a small subset, on the machine with the headroom. That is a much better trade than the first reading recorded.

**Reduce is unsolved for contact.** Averaging two branches of a stack gives interpenetration:
the mean of two valid states is not a valid state when the states satisfy contact constraints.
The deck's own worked example is a single Δv correction and still needs history rewinding.

**And their own stated disadvantages are the real ones**: "must follow paradigm" and
"fine-tuning". The weighted reduce only works if everything carries weights; this is not a
technique to adopt piecemeal.

### What this does not say

- Nothing here was measured. This is a reading of a slide deck against numbers taken elsewhere
  in this logbook, and every claim about XPR is theirs rather than tested.
- The `1/16 c` propagation figure and the log-depth routing check out arithmetically — 10^43
  CPUs is 143 butterfly hops, 143 × 5 ms across an Earth diameter — but arithmetic that checks
  out is not a result about this workspace.
- The one thing recommended for adoption, vector-clock independence detection, is recommended on
  reasoning and not on a measurement. It is cheap enough that the reasoning may be enough.
