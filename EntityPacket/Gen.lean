import EntityPacket.Codec

namespace EntityPacket

/-- Build a deterministic test packet from a seed integer.
    Drives both the golden-CSV emitter and the Plausible roundtrip harness. -/
def mk (n : Nat) : Packet :=
  let f : Nat → Int64 := fun k => Int64.ofInt (Int.ofNat ((n * 2654435761 + k * 40503) % 4000000000) - 2000000000)
  let g : Nat → Int16 := fun k => Int16.ofInt (Int.ofNat ((n + k) % 65535) - 32767)
  let u : Nat → UInt32 := fun k => UInt32.ofNat ((n * 7 + k) % 4294967296)
  { gid := u 1, posUm := (f 0, f 1, f 2), vel := (g 0, g 1, g 2),
    hlc := u 2, classOwner := u 3, subIndex := u 4, rot := (g 3, g 4, g 5),
    payload := (Array.range PAYLOAD_LEN).map (fun i => UInt8.ofNat ((n + i*31) % 256)) }

/-- Encode a byte array as a lowercase hex string. -/
def hex (b : Array UInt8) : String :=
  b.foldl (fun s x =>
    s ++ (Nat.toDigits 16 (x.toNat / 16)).asString
      ++ (Nat.toDigits 16 (x.toNat % 16)).asString) ""

end EntityPacket
