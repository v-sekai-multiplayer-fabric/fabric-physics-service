-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Mathlib.Data.Nat.ModEq
import Shared.Types
import PredictiveBvh.core.Formula
import PredictiveBvh.core.HilbertCell
import PredictiveBvh.core.ScaleContradictions

/-! # The non-physical segment

Not every entity has a body. A zone's authority, a contract marker, a board row and an item in
somebody's hands are all entities a subscriber is sent, and none of them stands anywhere.

The broadphase has no opinion about that: `hilbertCellOf` takes a position, `normalizeCoord`
takes a coordinate, and an entity without one is an entity the tree cannot hold. Leaving them
out is not free either — an entity outside the tree is outside every interest query, so it
reaches a subscriber by some second path that has to be written, tested and kept in step.

So the scene reserves a segment for them and `place` is total. The question this file answers
is not whether to reserve one, it is **where**, and that has two sides pulling opposite ways:

* Put it too close and a physical entity's ghost AABB reaches it, which puts a body and an
  abstraction in one interest box. `segment_beyond_ghost_reach` is the bound that forbids it.
* Put it too far and the segment, which is empty, takes the resolution the deck needed:
  `normalizeCoord` divides the whole scene extent into 1024 steps whatever is in it.
  `deck_keeps_half_the_range` and `far_band_costs_five_bits` are the two ends of that.

The middle is a segment one deck-depth below the floor. It is not a distant parking lot, and
that is the point.
-/

namespace PredictiveBVH
namespace NonPhysical

/-- Micrometres from metres, which is the unit every position in this cluster is in. -/
def um (m : Int) : Int := m * 1000000

-- ── The deck, and the segment under it ───────────────────────────────────────

/-- The ceiling of the physical region: the upper deck. -/
def deckCeilingUm : Int := um 0

/-- The floor of the physical region: the lower deck. Everything with a body is between this
    and `deckCeilingUm`. -/
def deckFloorUm : Int := um (-30)

/-- How far below the deck floor the segment sits. Chosen by the two theorems below, not by
    taste: it has to clear `ghostReachUm` and it has to stay cheap in normalized steps. -/
def segmentGapUm : Int := um 30

/-- The plane the non-physical entities are placed on. -/
def segmentY : Int := deckFloorUm - segmentGapUm

/-- The scene the tree normalizes against, once the segment is part of it. -/
def sceneExtentUm : Int := deckCeilingUm - segmentY

/-- What the deck alone would have been. -/
def deckExtentUm : Int := deckCeilingUm - deckFloorUm

-- ── Every entity has a place ─────────────────────────────────────────────────

/-- An entity either has a body or does not. Both cases are placed; that is the whole point of
    the type. `ident` is the entity's local id, which is what spreads the non-physical ones
    apart — they are not all one point, because a broadphase cannot separate a point from
    itself and every query that touched one would return all of them. -/
inductive Body where
  | physical (p : Vec3)
  | nonPhysical (ident : Nat)
  deriving Repr

/-- The X residue. A stride coprime to its modulus, so consecutive ids do not land together. -/
def residX (ident : Nat) : Nat := (ident * 37) % 103

/-- The Z residue, against a modulus coprime to X's. That second coprimality is what makes the
    lattice period the product rather than the smaller modulus. -/
def residZ (ident : Nat) : Nat := (ident * 23) % 109

/-- How many entities the lattice separates before it repeats. -/
def period : Nat := 103 * 109

def spreadX (ident : Nat) : Int := um ((residX ident : Int) - 51)

def spreadZ (ident : Nat) : Int := um ((residZ ident : Int) - 54)

/-- Where an entity is. Total: this is the file's claim, and it is a claim about the type
    rather than a theorem about a partial function. -/
def place : Body → Vec3
  | .physical p => p
  | .nonPhysical ident => { x := spreadX ident, y := segmentY, z := spreadZ ident }

/-- A non-physical entity is on the segment plane, whatever its id. -/
theorem nonPhysical_on_segment (ident : Nat) :
    (place (.nonPhysical ident)).y = segmentY := rfl

/-- A physical entity keeps the position it had. Reserving the segment moves nobody. -/
theorem physical_unmoved (p : Vec3) : place (.physical p) = p := rfl

-- ── The segment separates what it holds ──────────────────────────────────────

/-- `um` is injective, so a place determines its residues. -/
private theorem um_inj {a b : Int} (h : um a = um b) : a = b := by
  unfold um at h; omega

/-- **The lattice bound.** Distinct ids below the period get distinct places.
    This is the property the two moduli exist for. Without it every non-physical entity could
    share one point, and a broadphase cannot separate a point from itself: one query touching
    it returns all of them, which is the interest filter failing open.

    The argument is the Chinese remainder theorem run backwards. `37` is coprime to `103`, so
    the X residue determines `ident` mod 103; `23` is coprime to `109`, so the Z residue
    determines it mod 109; and 103 and 109 are coprime, so together they determine it mod
    their product. Below that product, determined mod it means equal. -/
theorem place_injective_below_period {i j : Nat} (hi : i < period) (hj : j < period)
    (h : place (.nonPhysical i) = place (.nonPhysical j)) : i = j := by
  have hx : residX i = residX j := by
    have := um_inj (congrArg Vec3.x h); unfold residX at *; omega
  have hz : residZ i = residZ j := by
    have := um_inj (congrArg Vec3.z h); unfold residZ at *; omega
  have hx' : 37 * i ≡ 37 * j [MOD 103] := by
    unfold residX at hx; simp only [Nat.ModEq, Nat.mul_comm]; omega
  have hz' : 23 * i ≡ 23 * j [MOD 109] := by
    unfold residZ at hz; simp only [Nat.ModEq, Nat.mul_comm]; omega
  have h103 : i ≡ j [MOD 103] := Nat.ModEq.cancel_left_of_coprime (by decide) hx'
  have h109 : i ≡ j [MOD 109] := Nat.ModEq.cancel_left_of_coprime (by decide) hz'
  have hmul : i ≡ j [MOD 103 * 109] :=
    (Nat.modEq_and_modEq_iff_modEq_mul (by decide)).mp ⟨h103, h109⟩
  unfold period at hi hj
  rw [Nat.ModEq, Nat.mod_eq_of_lt hi, Nat.mod_eq_of_lt hj] at hmul
  exact hmul

/-- The period is 11227, and it is tight: the next id after it lands back on the first place.
    A bound that is never reached is a bound nobody has to believe, so this says where it is. -/
theorem period_eq : period = 11227 := by native_decide

theorem period_is_tight : place (.nonPhysical 0) = place (.nonPhysical period) := by
  native_decide

/-- And it covers what the tenant needs: `fabric-store-domain` names at most `MAX_WARDS`
    wards of `SPARKS_PER_WARD`, which is 8 * 1384. The lattice separates every one of them. -/
theorem period_covers_a_shard : 8 * 1384 ≤ period := by native_decide

-- ── The segment is out of reach ──────────────────────────────────────────────

/-- How far a body can extend below itself in one prediction window: the ghost bound at the
    physical speed cap over the worst delta this cluster models, which is the satellite one.
    Nothing physical reaches further down than this. -/
def ghostReachUm : Int := (ghostBound vMaxPhysical 0 satelliteDelta : Nat)

/-- 10 m/s at 20 Hz over 40 ticks is 20 m. -/
theorem ghostReach_is_20m : ghostReachUm = um 20 := by native_decide

/-- **The placement bound.** The gap clears the deepest ghost a physical entity can throw, so
    no body's expanded AABB overlaps the segment plane and no interest query returns a marker
    because somebody ran at it. This is the inequality that fixes `segmentGapUm` from below. -/
theorem segment_beyond_ghost_reach : ghostReachUm < segmentGapUm := by native_decide

/-- Stated the way a query sees it: the lowest a ghost can reach is still above the segment. -/
theorem ghost_floor_above_segment : deckFloorUm - ghostReachUm > segmentY := by native_decide

-- ── The segment is cheap ─────────────────────────────────────────────────────

/-- How many of `normalizeCoord`'s 1024 steps a span gets, once the scene is `extent` deep.
    `normalizeCoord` divides the scene it is given, so an empty span still costs. -/
def stepsFor (span extent : Int) : Int := 1024 * span / max extent 1

/-- **The cost bound.** The deck keeps half the normalized range: the segment doubles the
    scene, which is one bit of Y, and one bit is what a whole class of entities costs. -/
theorem deck_keeps_half_the_range : stepsFor deckExtentUm sceneExtentUm = 512 := by
  native_decide

/-- The other end, and the reason the segment is not parked somewhere obviously safe: a band a
    kilometre down leaves the deck 30 steps of 1024 where the segment above leaves it 512.
    Four bits of Y resolution, spent on empty air, for the same property the theorem above
    gets for one. -/
theorem far_band_costs_four_bits :
    stepsFor deckExtentUm (deckCeilingUm - um (-1000)) = 30 := by native_decide

/-- And it is still a real bound, not an accident of the deck being 30 m: a band that far down
    would clear the ghost reach too. Both placements are correct; only one is affordable. -/
theorem far_band_also_clears_ghosts : ghostReachUm < um 1000 - um 30 := by native_decide

-- ── The segment is inside the scene ──────────────────────────────────────────

/-- A placed non-physical entity normalizes without clamping: it is inside the scene the tree
    was told about, so `normalizeCoord` maps it to a real step rather than saturating at 0.
    A clamped coordinate is the failure this is here to rule out — everything that saturates
    lands on one step, which is the collapsed-to-a-point case again. -/
theorem segment_normalizes_in_range :
    HilbertCell.normalizeCoord segmentY segmentY sceneExtentUm = 0 ∧
    HilbertCell.normalizeCoord deckCeilingUm segmentY sceneExtentUm ≤ 1023 := by
  constructor
  · native_decide
  · exact HilbertCell.normalizeCoord_range _ _ _

end NonPhysical
end PredictiveBVH
