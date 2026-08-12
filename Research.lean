-- Non-gated research tier (NOT on the CI production gate). Build with
-- `lake build Research`.
--   Partition         — research-tier optimal-partition proofs
--   TemporalCoherence — codeStable predicate, pairsUnchanged theorem,
--                       TemporalBroadphaseState incremental tick model
import PredictiveBvh.core.Partition
import PredictiveBvh.core.TemporalCoherence
