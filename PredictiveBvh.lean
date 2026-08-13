-- PredictiveBvh hexagon cluster aggregator — production tier.
-- Spatial oracle: ghost expansion + SAH + broadphase (core), AmoLean codegen (adapters).
-- Research-tier Partition lives in Research.lean (off the CI gate).
import PredictiveBvh.adapters.CodeGen
import PredictiveBvh.adapters.GodotBinary
import PredictiveBvh.adapters.QuinticHermite
import PredictiveBvh.adapters.RingOps
import PredictiveBvh.adapters.TreeC
import PredictiveBvh.core.BucketBound
import PredictiveBvh.core.BucketDir
import PredictiveBvh.core.CurveContract
import PredictiveBvh.core.CurveDuality
import PredictiveBvh.core.EMLAdversarialHeuristic
import PredictiveBvh.core.Formula
import PredictiveBvh.core.HilbertBroadphase
import PredictiveBvh.core.HilbertCell
import PredictiveBvh.core.HilbertRoundtrip
import PredictiveBvh.core.LowerBound
import PredictiveBvh.core.NonPhysical
import PredictiveBvh.core.Resources
import PredictiveBvh.core.ScaleContradictions
import PredictiveBvh.core.ScaleProofs
