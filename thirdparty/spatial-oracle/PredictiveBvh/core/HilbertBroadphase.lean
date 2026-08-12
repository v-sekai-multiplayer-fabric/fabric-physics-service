-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Shared.Types
import PredictiveBvh.core.LowerBound
import PredictiveBvh.core.BucketDir

-- ============================================================================
-- OVERLAP-ADAPTIVE HILBERT BROADPHASE
--
-- O(N + k) broadphase that reports all overlapping pairs among N ghost-expanded
-- AABBs.  Operates on Array BoundingBox — no dependency on SimEntity/GhostSnap.
--
-- Algorithm:
--   1. Compute scene AABB (union of all ghost AABBs)
--   2. Hilbert-code each AABB centroid, sort by code   — O(N) radix sort
--   3. Form groups by Hilbert density (sz=4 dense, sz=16 sparse, sz=8 default)
--   4. BucketDir inter-group sweep (k_bucket ≤ 32)     — O(N + k)
--   5. Intra-group: check all C(sz,2) pairs             — O(N)
--   6. Report overlapping pairs
--
-- Constant factor (per tick):
--   radix(3N) + groups(N) + inter(N/8×32) + intra(N/8×28=3.5N) + k
--   ≈ ~7.5N + k for N≥500   [inter-group drops from 3√G to k_bucket≤32]
--   → BucketDir lookup replaces linear window scan for scalable inter-group sweep
--   → adaptive group size: sz=4 in dense regions (tight AABBs), sz=16 sparse
--
-- Zone partition (authority/interest fanout):
--   partitionByZone divides sorted entries into 2^zoneBits zones by Hilbert
--   code prefix.  Each zone can run an independent hilbertBroadphase
--   (authority) and exchange cross-zone interest pairs via interestEntries.
--   Future: map each zone to a separate core or server for linear scale-out.
--
-- Proved:
--   - hilbert_prune_sound: non-overlapping group AABBs → no entity overlap
--   - aabbOverlapsDec_false_implies_disjoint
--   - avg_bucket_bound: k_bucket ≤ 32 per BucketBound.lean
-- ============================================================================

-- ── Data structures ──────────────────────────────────────────────────────────

structure HilbertEntry where
  id    : Nat
  code  : Nat
  ghost : BoundingBox
  deriving Inhabited

structure HilbertGroup where
  first : Nat          -- start index in sorted array
  last  : Nat          -- end index (inclusive)
  aabb  : BoundingBox  -- union of member ghost AABBs
  deriving Inhabited

structure BroadphaseResult where
  pairs       : Array (Nat × Nat)  -- overlapping pairs (i < j by original id)
  pairsFound  : Nat
  pairsPruned : Nat
  totalWork   : Nat
  groupCount  : Nat

-- ── AABB overlap test ────────────────────────────────────────────────────────

def aabbOverlapsDec (A B : BoundingBox) : Bool :=
  A.minX ≤ B.maxX && B.minX ≤ A.maxX &&
  A.minY ≤ B.maxY && B.minY ≤ A.maxY &&
  A.minZ ≤ B.maxZ && B.minZ ≤ A.maxZ

-- ── Step 1: Scene bounds ─────────────────────────────────────────────────────

def computeSceneBounds (ghosts : Array BoundingBox) : BoundingBox :=
  if ghosts.isEmpty then { minX := 0, maxX := 1, minY := 0, maxY := 1, minZ := 0, maxZ := 1 }
  else ghosts.foldl unionBounds ghosts[0]!

-- ── Step 2: Hilbert codes (Skilling 2004, better volume locality) ────────────

def hilbertOfBox (b : BoundingBox) (scene : BoundingBox) : Nat :=
  let cx := (b.minX + b.maxX) / 2
  let cy := (b.minY + b.maxY) / 2
  let cz := (b.minZ + b.maxZ) / 2
  let sw := max (scene.maxX - scene.minX) 1
  let sh := max (scene.maxY - scene.minY) 1
  let sd := max (scene.maxZ - scene.minZ) 1
  let nx := ((cx - scene.minX) * 1024 / sw).toNat.min 1023
  let ny := ((cy - scene.minY) * 1024 / sh).toNat.min 1023
  let nz := ((cz - scene.minZ) * 1024 / sd).toNat.min 1023
  hilbert3D nx ny nz

-- LSD radix sort on 30-bit Hilbert codes — O(N).
-- Per LowerBound.lean: "Radix sort: O(N) — non-comparison, integer arithmetic".
-- Three passes of counting sort over 10-bit buckets (1024 buckets each).
-- Each pass: O(N + 1024) = O(N). Total: O(N).

/-- One counting-sort pass over the 10-bit field `(entry.code >>> shift) &&& 0x3FF`. -/
private def countingSortPass (arr : Array HilbertEntry) (shift : Nat) : Array HilbertEntry :=
  let B := 1024
  -- Count occurrences per bucket
  let counts := arr.foldl (fun (acc : Array Nat) e =>
    let b := (e.code >>> shift) &&& (B - 1)
    acc.set! b (acc[b]! + 1)) (Array.replicate B 0)
  -- Prefix sums → write positions
  let prefixes := Id.run do
    let mut pref : Array Nat := Array.replicate B 0
    let mut total := 0
    for i in List.range B do
      let c := counts[i]!
      pref := pref.set! i total
      total := total + c
    return pref
  -- Scatter into output
  Id.run do
    let mut out : Array HilbertEntry := Array.replicate arr.size default
    let mut pref := prefixes
    for e in arr do
      let b := (e.code >>> shift) &&& (B - 1)
      let idx := pref[b]!
      out  := out.set!  idx e
      pref := pref.set! b (idx + 1)
    return out

/-- Three-pass LSD radix sort: bits 0–9, then 10–19, then 20–29. -/
private def radixSortEntries (arr : Array HilbertEntry) : Array HilbertEntry :=
  countingSortPass (countingSortPass (countingSortPass arr 0) 10) 20

def sortByHilbert (ghosts : Array BoundingBox) : Array HilbertEntry :=
  let scene := computeSceneBounds ghosts
  let entries := ghosts.mapIdx fun i b => { id := i, code := hilbertOfBox b scene, ghost := b }
  radixSortEntries entries

/-- Like sortByHilbert but uses caller-supplied scene bounds instead of computing
    them from the ghosts.  Required for temporal coherence: if bounds are
    recomputed each tick, a boundary entity moving by 1 unit changes sw and
    remaps ALL codes — making every group appear dirty.  Fixed bounds isolate
    code changes to entities that actually moved. -/
def sortByHilbertFixed (ghosts : Array BoundingBox) (scene : BoundingBox) : Array HilbertEntry :=
  let entries := ghosts.mapIdx fun i b => { id := i, code := hilbertOfBox b scene, ghost := b }
  radixSortEntries entries

-- ── Step 3: Adaptive grouping ────────────────────────────────────────────────

private def groupUnion (sorted : Array HilbertEntry) (first last : Nat) : BoundingBox :=
  let init := sorted[first]!.ghost
  (List.range (last - first)).foldl (fun acc j =>
    unionBounds acc (sorted[first + j + 1]!).ghost) init

-- Adaptive group size: small groups in dense Hilbert regions (small code gaps)
-- → tighter AABBs → fewer false-positive inter-group checks. Large groups in
-- sparse regions (large gaps) → low pair probability, reduced overhead.
private def adaptiveGroupSize (gap : Nat) (n : Nat) : Nat :=
  if n == 0 then 8
  else
    let codeSpace := 1 <<< 30  -- 30-bit Hilbert code space
    let denseThreshold := codeSpace / (n * 4)
    let sparseThreshold := codeSpace / n * 4
    if gap < denseThreshold then 4
    else if gap > sparseThreshold then 16
    else 8

def formGroups (sorted : Array HilbertEntry) (maxGroupSize : Nat := 8) : Array HilbertGroup :=
  let n := sorted.size
  if n == 0 then #[]
  else Id.run do
    let mut groups : Array HilbertGroup := #[]
    let mut i := 0
    while i < n do
      -- Choose group size based on local Hilbert code gap (density)
      let gap := if i + 1 < n then sorted[i + 1]!.code - sorted[i]!.code else 0
      let sz := if maxGroupSize == 8 then adaptiveGroupSize gap n else maxGroupSize
      -- Greedy: extend up to sz or until Hilbert prefix diverges
      let endCand := min (i + sz - 1) (n - 1)
      let xorVal := sorted[i]!.code ^^^ sorted[endCand]!.code
      let pfxDepth := clz30 xorVal
      let pfxBits := sorted[i]!.code >>> (30 - pfxDepth)
      let mut j := i
      while j + 1 < n && j - i < sz &&
            sorted[j + 1]!.code >>> (30 - pfxDepth) == pfxBits do
        j := j + 1
      let aabb := groupUnion sorted i j
      groups := groups.push { first := i, last := j, aabb }
      i := j + 1
    return groups

-- ── Steps 4-5: BucketDir inter-group scan ────────────────────────────────────
-- Build a bucket directory over the groups array keyed by each group's
-- representative Hilbert code (first entry's code).  For each gi, look up
-- the bucket window [lo, hi) and check only those groups.
--
-- Complexity: O(G × k_bucket + k) = O(N + k) where k_bucket ≤ kTarget = 32
-- (proved by BucketBound.avg_bucket_bound).  Also always checks gi+1 to
-- handle pairs that straddle a bucket boundary.
--
-- Replaces the previous adaptive-window forward scan (O(G × 3√G) = O(N^1.5/const)).
-- At N=5000 (G≈625): 75 → 32 candidates per group = 2.3× fewer inter-group checks.

private def buildGroupDir (sorted : Array HilbertEntry) (groups : Array HilbertGroup)
    : Array (Nat × Nat) :=
  let G := groups.size
  let bb := PredictiveBVH.BucketBound.bucketBitsFor G
  let leaves : PredictiveBVH.BucketDir.SortedLeaves := Array.range G
  PredictiveBVH.BucketDir.buildBucketDir leaves
    (fun gi => sorted[groups[gi]!.first]!.code) bb

private structure BPAccum where
  pairs   : Array (Nat × Nat) := #[]
  pruned  : Nat := 0
  work    : Nat := 0

private def checkInterGroup (sorted : Array HilbertEntry) (g1 g2 : HilbertGroup) (acc : BPAccum) : BPAccum :=
  let sz1 := g1.last - g1.first + 1
  let sz2 := g2.last - g2.first + 1
  if aabbOverlapsDec g1.aabb g2.aabb then
    (List.range sz1).foldl (fun acc1 ii =>
      (List.range sz2).foldl (fun acc2 jj =>
        let e1 := sorted[g1.first + ii]!
        let e2 := sorted[g2.first + jj]!
        let acc2 := { acc2 with work := acc2.work + 1 }
        if aabbOverlapsDec e1.ghost e2.ghost then
          let p := if e1.id < e2.id then (e1.id, e2.id) else (e2.id, e1.id)
          { acc2 with pairs := acc2.pairs.push p }
        else acc2) acc1) acc
  else { acc with pruned := acc.pruned + sz1 * sz2 }

private def checkIntraGroup (sorted : Array HilbertEntry) (g : HilbertGroup) (acc : BPAccum) : BPAccum :=
  let sz := g.last - g.first + 1
  (List.range sz).foldl (fun acc1 ii =>
    (List.range sz).foldl (fun acc2 jj =>
      if ii < jj then
        let e1 := sorted[g.first + ii]!
        let e2 := sorted[g.first + jj]!
        let acc2 := { acc2 with work := acc2.work + 1 }
        if aabbOverlapsDec e1.ghost e2.ghost then
          let p := if e1.id < e2.id then (e1.id, e2.id) else (e2.id, e1.id)
          { acc2 with pairs := acc2.pairs.push p }
        else acc2
      else acc2) acc1) acc

/-- Inter-group sweep using a bucket directory: for each gi look up the
    bucket window [lo, hi) and check only gj > gi in that bucket.  Also
    checks gi+1 unconditionally to catch pairs that straddle a bucket boundary.
    O(G × k_bucket + k) = O(N + k) by BucketBound.avg_bucket_bound. -/
private def interGroupSweep (sorted : Array HilbertEntry) (groups : Array HilbertGroup) : BPAccum :=
  let G := groups.size
  let bb := PredictiveBVH.BucketBound.bucketBitsFor G
  let dir := buildGroupDir sorted groups
  Id.run do
    let mut acc : BPAccum := {}
    for gi in List.range G do
      -- Always check the immediate successor to handle cross-bucket pairs
      if gi + 1 < G then
        acc := checkInterGroup sorted groups[gi]! groups[gi + 1]! acc
      -- Bucket lookup: find all groups sharing gi's Hilbert prefix bucket
      let code := sorted[groups[gi]!.first]!.code
      let b := PredictiveBVH.BucketDir.bucketOf code bb
      if b < dir.size then
        let (lo, hi) := dir[b]!
        for gj in List.range (Nat.min hi G - lo) do
          let gjIdx := lo + gj
          -- Skip gi itself and gi+1 (already checked above)
          if gjIdx > gi + 1 then
            acc := checkInterGroup sorted groups[gi]! groups[gjIdx]! acc
    return acc

def hilbertBroadphase (ghosts : Array BoundingBox) : BroadphaseResult :=
  let sorted := sortByHilbert ghosts
  let groups := formGroups sorted
  let G := groups.size
  -- Inter-group: adaptive forward sweep O(G × √G + k) = O(N + k)
  let acc := interGroupSweep sorted groups
  -- Intra-group: O(N)
  let acc := (List.range G).foldl (fun acc gi =>
    checkIntraGroup sorted groups[gi]! acc) acc
  -- No dedup needed: inter-group uses gi<gj, intra-group uses ii<jj,
  -- and pair IDs are normalized to (min, max).
  { pairs := acc.pairs, pairsFound := acc.pairs.size, pairsPruned := acc.pruned,
    totalWork := acc.work, groupCount := G }

-- ── Step 6: Brute-force baseline ─────────────────────────────────────────────

def bruteForceOverlap (ghosts : Array BoundingBox) : Array (Nat × Nat) :=
  let n := ghosts.size
  Id.run do
    let mut pairs : Array (Nat × Nat) := #[]
    for i in List.range n do
      for j in List.range n do
        if i < j then
          if aabbOverlapsDec ghosts[i]! ghosts[j]! then
            pairs := pairs.push (i, j)
    return pairs

-- ── Cross-validation ─────────────────────────────────────────────────────────

/-- Check that every brute-force pair appears in the Hilbert result.
    Returns the number of mismatches (should be 0). -/
def validateHilbertVsBrute (result : BroadphaseResult) (brute : Array (Nat × Nat)) : Nat :=
  brute.foldl (fun misses p =>
    if result.pairs.contains p then misses else misses + 1) 0

-- ── Zone-based authority / interest partition ─────────────────────────────────
-- Divides sorted entries by the top `zoneBits` bits of their 30-bit Hilbert
-- code into 2^zoneBits zones.  Each zone is an independent broadphase unit
-- (authority).  A zone's interest set expands by `interestRadius` zones on
-- each side to capture players near zone boundaries.
--
-- Complexity:
--   partitionByZone : O(N)          — single pass, stable
--   authorityEntries: O(1)          — index lookup
--   interestEntries : O(interestRadius × N/numZones) — bounded slice concat
--
-- Scale-out path:
--   Run hilbertBroadphase on each zone's interestEntries independently.
--   Each zone → separate core or server; cross-zone pairs handled by the
--   authority fanout (only authority-zone players *send*; interest-zone
--   players *receive*).

structure ZonePartition where
  zoneBits : Nat
  zones    : Array (Array HilbertEntry)

/-- Partition sorted entries into 2^zoneBits zones by Hilbert code prefix. -/
def partitionByZone (sorted : Array HilbertEntry) (zoneBits : Nat := 6) : ZonePartition :=
  let numZones := 1 <<< zoneBits
  let shift    := 30 - zoneBits
  Id.run do
    let mut zones : Array (Array HilbertEntry) := Array.replicate numZones #[]
    for e in sorted do
      let z := (e.code >>> shift) % numZones
      zones := zones.set! z (zones[z]!.push e)
    return { zoneBits, zones }

/-- Entries whose Hilbert prefix belongs to zone z (the authority set). -/
def authorityEntries (zp : ZonePartition) (z : Nat) : Array HilbertEntry :=
  if z < zp.zones.size then zp.zones[z]! else #[]

/-- Entries from zones [z−r … z+r] (clamped): the interest set for zone z.
    `interestRadius` must cover max spatial separation across a zone boundary
    (i.e., ≥ AOI_radius / zone_spatial_extent). -/
def interestEntries (zp : ZonePartition) (z : Nat) (interestRadius : Nat := 1) : Array HilbertEntry :=
  let numZones := zp.zones.size
  let lo := if z ≥ interestRadius then z - interestRadius else 0
  let hi := Nat.min numZones (z + interestRadius + 1)
  Id.run do
    let mut acc : Array HilbertEntry := #[]
    for dz in List.range (hi - lo) do
      acc := acc ++ zp.zones[lo + dz]!
    return acc

-- ============================================================================
-- PROOFS
-- ============================================================================

/-- The decidable AABB overlap test agrees with the propositional definition:
    aabbOverlapsDec returns false → aabbDisjoint holds. -/
theorem aabbOverlapsDec_false_implies_disjoint (A B : BoundingBox)
    (h : aabbOverlapsDec A B = false) : aabbDisjoint A B := by
  simp only [aabbOverlapsDec, aabbDisjoint, intervalsDisjoint] at *
  revert h; grind

/-- Pruning soundness: composes the decidable disjoint test with
    overlap_prune_sound from LowerBound.lean.
    If two groups' AABBs don't overlap (decidable test returns false),
    then no entity in group 1 overlaps any entity in group 2. -/
theorem hilbert_prune_sound (G₁ G₂ e₁ e₂ : BoundingBox)
    (hc1 : aabbContains G₁ e₁) (hc2 : aabbContains G₂ e₂)
    (h : aabbOverlapsDec G₁ G₂ = false) :
    aabbOverlapsDec e₁ e₂ = false := by
  have hdis := aabbOverlapsDec_false_implies_disjoint G₁ G₂ h
  have edis := overlap_prune_sound G₁ G₂ e₁ e₂ hc1 hc2 hdis
  simp only [aabbOverlapsDec, aabbDisjoint, intervalsDisjoint] at *
  revert edis; grind
