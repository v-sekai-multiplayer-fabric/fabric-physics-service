-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Shared.Types

-- ============================================================================
-- WHAT THIS REPOSITORY REQUIRES OF THE CURVE IT IS PINNED TO
--
-- `lean-shared-core` gates `hilbert3D` on its own side, and that gate is correct
-- and did its job. It also runs nowhere near the failure this file exists for.
--
-- `lake-manifest.json` pins `lean-shared-core` by REV, not by branch. `inputRev`
-- reads `main`, but `rev` is a fixed commit and Lake moves it only on `lake
-- update`. So an upstream fix can merge and change nothing here, and an upstream
-- rewrite can change the curve under this repository without any diff in it.
--
-- That is not hypothetical. The forward fix and the matching inverse merged
-- eleven seconds apart, in the right order, both landing every commit — and this
-- repository stayed broken afterwards, holding the corrected inverse against the
-- old forward, because the pin had not moved. Both round trips still closed. They
-- closed on the wrong cell, which is the same way the original defect survived.
--
-- So the contract is asserted HERE, against whatever encoder is actually pinned,
-- rather than trusted because a dependency asserts it about itself. A stale or
-- rolled-back pin now fails this build instead of silently changing the curve.
--
-- Kept deliberately cheap. The exhaustive version lives upstream where the
-- implementation does; this is a tripwire, and a tripwire that costs a second is
-- one nobody deletes.
-- ============================================================================

namespace CurveContract

/-- The pinned encoder over an `n`-cube, in code order. A Hilbert curve traverses
    any aligned subcube contiguously, so a corner is a sound witness. -/
def walk (n : Nat) : List (Nat × Nat × Nat) :=
  (((List.range n).flatMap fun x => (List.range n).flatMap fun y => (List.range n).map fun z =>
      (hilbert3D x y z, (x, y, z))).mergeSort (fun a b => a.1 ≤ b.1)).map (fun p => p.2)

private def l1 (a b : Nat × Nat × Nat) : Nat :=
  (max a.1 b.1 - min a.1 b.1) + (max a.2.1 b.2.1 - min a.2.1 b.2.1)
    + (max a.2.2 b.2.2 - min a.2.2 b.2.2)

/-- Consecutive codes that are not face-adjacent. Zero for a Hilbert curve. The
    encoder this repository shipped against scored 87.5% here. -/
def contiguityDefects (n : Nat) : Nat :=
  let w := walk n
  ((w.zip w.tail).map (fun p => l1 p.1 p.2)).countP (· != 1)

/-- THE TRIPWIRE. Not "is the dependency's test passing" — that test is not run by
    this build — but "is the curve we are compiled against a Hilbert curve". -/
theorem pinned_curve_is_contiguous : contiguityDefects 8 = 0 := by native_decide

/-- A canary on the exact values, because contiguity alone does not pin down which
    Hilbert curve. Any change to the encoder — orientation, bit order, order
    parameter — moves these, and moving them silently is the failure mode. The
    broken encoder gave `hilbert3D 512 0 0 = 1073741823`, an edge cell handed the
    very last index, which is the shape of the defect in one number. -/
theorem pinned_curve_values :
    hilbert3D 0 0 0 = 0 ∧
    hilbert3D 512 0 0 = 1011426450 ∧
    hilbert3D 1023 1023 1023 = 766958445 := by native_decide

end CurveContract
