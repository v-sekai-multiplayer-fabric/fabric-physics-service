-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import PlausibleWitnessDag
import PredictiveBvh.core.HilbertBroadphase
import PredictiveBvh.core.TemporalCoherence

-- ============================================================================
-- ZONE CONCURRENCY HILL-CLIMB (cold + temporal)
--
-- Cold bench: single-tick hilbertBroadphase, averaged over numSeeds seeds.
--   Theoretical ceiling: 50,000 / 13.5N ≈ 3,700; empirical ≈ 2,133.
--
-- Temporal bench: N players, 2% move 1 unit/tick via LCG.
--   After 5 warmup ticks, average tickBroadphase.totalWork over 10 steady ticks.
--   Theoretical ceiling (intra-only floor):  50,000 / 3.5N ≈ 14,285.
--   Temporal ceiling (2% dirty, ~30% group mask): 50,000 / ~4N ≈ 12,500.
-- ============================================================================

namespace ZoneConcurrency

open PlausibleWitnessDag

-- ── Scene parameters ──────────────────────────────────────────────────────────

def zoneSize  : Int := 120
def ghostHalf : Int := 1

/-- Work budget: max avg totalWork per zone per tick. -/
def workBudget : Nat := 50000

/-- Number of random seeds averaged per candidate N to reduce variance. -/
def numSeeds : Nat := 4

-- ── Deterministic player generation ──────────────────────────────────────────

private def lcgStep (s : Nat) : Nat :=
  (s * 6364136223846793005 + 1442695040888963407) % (2 ^ 64)

private def playerBox (seed : Nat) : BoundingBox :=
  let s1 := lcgStep seed
  let s2 := lcgStep s1
  let cx : Int := (Int.ofNat (s1 % zoneSize.toNat)) - zoneSize / 2
  let cz : Int := (Int.ofNat (s2 % zoneSize.toNat)) - zoneSize / 2
  { minX := cx - ghostHalf, maxX := cx + ghostHalf,
    minY := -ghostHalf,     maxY := ghostHalf,
    minZ := cz - ghostHalf, maxZ := cz + ghostHalf }

def generatePlayers (n : Nat) (baseSeed : Nat := 0xDEADBEEF) : Array BoundingBox := Id.run do
  let mut boxes : Array BoundingBox := #[]
  let mut seed  : Nat := baseSeed
  for _ in List.range n do
    boxes := boxes.push (playerBox seed)
    seed  := lcgStep seed
  return boxes

-- ── Cold broadphase work oracle ───────────────────────────────────────────────

private def seeds : List Nat :=
  [0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xABCDEF01]

def zoneWork (n : Nat) : Nat :=
  let total := seeds.take numSeeds |>.foldl (fun acc s =>
    acc + (hilbertBroadphase (generatePlayers n s)).totalWork) 0
  total / numSeeds

def withinBudget (n : Nat) : Bool := zoneWork n ≤ workBudget

-- ── Temporal motion: 2% of entities move 1 Hilbert code step/tick ───────────

/-- Fixed scene bounds for Hilbert code assignment: padded beyond the generation
    envelope so entity movement never changes scene width (sw). -/
def fixedSceneBounds : BoundingBox :=
  let pad : Int := 3
  { minX := -(zoneSize / 2 + ghostHalf + pad), maxX := zoneSize / 2 + ghostHalf + pad,
    minY := -(ghostHalf + pad),                maxY := ghostHalf + pad,
    minZ := -(zoneSize / 2 + ghostHalf + pad), maxZ := zoneSize / 2 + ghostHalf + pad }

/-- Advance a Hilbert-sorted array: ~2% of entities get code+1 and ghost minX+1.
    Moving in code space (not world space) guarantees a +1 code change per moved
    entity.  World-space movement crosses Hilbert-curve folds, jumping codes by
    millions and displacing the entity to a random sorted rank — making O(N)
    positions appear dirty even though only O(0.04N) entities actually moved. -/
private def tickSorted (base : Array HilbertEntry) (tick : Nat) : Array HilbertEntry :=
  base.mapIdx fun _ e =>
    let s := lcgStep (e.id * 7919 + tick * 104729)
    if s % 50 == 0 then
      { e with code  := e.code + 1,
               ghost := { e.ghost with minX := e.ghost.minX + 1, maxX := e.ghost.maxX + 1 } }
    else e

private def initTemporalState (base : Array HilbertEntry) : TemporalBroadphaseState :=
  let groups := formGroups base
  { prevSorted := base, prevGroups := groups,
    dirtyGroups := Array.replicate groups.size true }

/-- Steady-state work: average tickBroadphase.totalWork over `steadyTicks` ticks
    after `warmupTicks` of warmup.  Uses code-space movement so only truly-moved
    entities are detected as dirty. -/
def steadyStateWork (n : Nat) (baseSeed : Nat) (warmupTicks steadyTicks : Nat) : Nat :=
  let baseSorted := sortByHilbertFixed (generatePlayers n baseSeed) fixedSceneBounds
  let initState  := initTemporalState baseSorted
  let initResult : BroadphaseResult :=
    { pairs := #[], pairsFound := 0, pairsPruned := 0, totalWork := 0, groupCount := 0 }
  let (_, afterWarmup) := List.range warmupTicks |>.foldl (fun acc tick =>
    let (res, st) := acc
    tickBroadphase st (tickSorted baseSorted tick) res) (initResult, initState)
  let (totalWork, _, _) := List.range steadyTicks |>.foldl
    (fun acc tick =>
      let (w, res, st) := acc
      let (newRes, newSt) := tickBroadphase st (tickSorted baseSorted (warmupTicks + tick)) res
      (w + newRes.totalWork, newRes, newSt))
    (0, initResult, afterWarmup)
  totalWork / steadyTicks

/-- Average steadyStateWork over numSeeds seeds. -/
def temporalWork (n : Nat) : Nat :=
  let total := seeds.take 2 |>.foldl (fun acc s =>
    acc + steadyStateWork n s 5 5) 0
  total / 2

def withinBudgetTemporal (n : Nat) : Bool := temporalWork n ≤ workBudget

-- ── Witness ladders ───────────────────────────────────────────────────────────

def concurrentLadder : Array Level := #[
  { idx := 0, walkSteps := 256,   finBound := 256,   numInst := 300  },
  { idx := 1, walkSteps := 1024,  finBound := 1024,  numInst := 600  },
  { idx := 2, walkSteps := 4096,  finBound := 4096,  numInst := 1200 } ]

def temporalLadder : Array Level := #[
  { idx := 0, walkSteps := 2048,  finBound := 2048,  numInst := 300 },
  { idx := 1, walkSteps := 8192,  finBound := 8192,  numInst := 600 },
  { idx := 2, walkSteps := 32768, finBound := 32768, numInst := 900 } ]

-- ── Readback helpers ──────────────────────────────────────────────────────────

def readback (steps : Nat) : Readback Nat := Id.run do
  let mut best : Nat := 0
  for n in List.range (steps + 1) do
    if n > 0 && withinBudget n then best := n
  let budgetHit := best == steps
  let found := best > 0 && !budgetHit
  return { value := best, found, witnessIdx := best, budgetHit }

/-- Binary-search readback: temporalWork is roughly monotone in N, so O(log steps)
    evaluations suffice.  Each evaluation at large N is ~40ms; linear scan would be
    O(steps × 40ms) = minutes.  Binary search: O(log steps × 40ms) = seconds. -/
def readbackTemporal (steps : Nat) : Readback Nat := Id.run do
  let mut lo : Nat := 1
  let mut hi : Nat := steps
  let mut best : Nat := 0
  while lo ≤ hi do
    let mid := (lo + hi) / 2
    if withinBudgetTemporal mid then
      best := mid
      lo   := mid + 1
    else
      if hi == 0 then break
      hi := mid - 1
  let found     := best > 0 && best < steps
  let budgetHit := best == steps
  return { value := best, found, witnessIdx := best, budgetHit }

-- ── Hill-climb entry points ───────────────────────────────────────────────────

def hillClimb : IO Nat := do
  let (best, lvl, trace) ← resolve "max zone concurrents"
    (fun _lvl k => k > 0 && withinBudget k) readback concurrentLadder
  IO.println s!"Cold: max={best}  level=L{lvl}  outcome={repr trace.outcome}"
  return best

def hillClimbTemporal : IO Nat := do
  let (best, lvl, trace) ← resolve "max zone concurrents (temporal)"
    (fun _lvl k => k > 0 && withinBudgetTemporal k) readbackTemporal temporalLadder
  IO.println s!"Temporal: max={best}  level=L{lvl}  outcome={repr trace.outcome}"
  return best

-- ── Fabric-wide concurrent capacity ──────────────────────────────────────────

def sceneWidth   : Nat := 400
def sceneDepth   : Nat := 400
def zoneCellSize : Nat := 60

def zoneCols  : Nat := (sceneWidth  + zoneCellSize - 1) / zoneCellSize + 1  -- 8
def zoneRows  : Nat := (sceneDepth  + zoneCellSize - 1) / zoneCellSize + 1  -- 8
def totalZones : Nat := zoneCols * zoneRows  -- 64

def fabricMax (nPerZone : Nat) : Nat := nPerZone * totalZones

-- ── Theoretical ceilings ─────────────────────────────────────────────────────

/-- Intra-only floor: 3.5 ops/entity.  Upper bound on any scheme. -/
def theoreticalCeiling : Nat := workBudget / 35 * 10  -- 50000 / 3.5 = 14285

/-- Temporal ceiling estimate: 2% dirty × 15% group dirty rate → ~30% group mask
    → ~4 ops/entity steady state.  Upper bound on temporal scheme. -/
def temporalCeiling : Nat := workBudget / 4  -- ≈ 12500

end ZoneConcurrency

def main (_args : List String) : IO Unit := do
  IO.println "=== Zone Concurrency Bench ==="
  let coldMax  ← ZoneConcurrency.hillClimb
  let tempMax  ← ZoneConcurrency.hillClimbTemporal
  IO.println ""
  IO.println s!"Cold  max/zone: {coldMax}   fabric: {ZoneConcurrency.fabricMax coldMax}"
  IO.println s!"Temp  max/zone: {tempMax}   fabric: {ZoneConcurrency.fabricMax tempMax}"
  IO.println s!"Theoretical ceiling (intra-only): {ZoneConcurrency.theoreticalCeiling}/zone"
  IO.println s!"Temporal ceiling estimate:        {ZoneConcurrency.temporalCeiling}/zone"
  IO.println s!"Zones ({ZoneConcurrency.zoneCols}×{ZoneConcurrency.zoneRows}={ZoneConcurrency.totalZones})"
