import EntityPacket.Gen
import EntityPacket.EmitC
open EntityPacket

-- Two outputs, and they are the same claim twice.
--
-- `packet_golden.csv` says what the bytes are for 64 known packets; `xr_grid_entity_packet.h`
-- says how to make them in C. Emitting both from one run is what lets `test/golden.c` hold the
-- second to the first: a header generated from the spec is a claim about provenance, and only
-- the golden vectors make it a claim about bytes.
def main : IO Unit := do
  IO.FS.createDirAll "build"
  let mut out := "hex,gid,pumx,pumy,pumz,velx,vely,velz,pay0,pay41\n"
  for n in [0:64] do
    let p := mk n
    let b := encode p
    out := out ++ s!"{hex b},{p.gid},{p.posUm.1},{p.posUm.2.1},{p.posUm.2.2},{p.vel.1},{p.vel.2.1},{p.vel.2.2},{p.payload[0]!},{p.payload[41]!}\n"
  IO.FS.writeFile "build/packet_golden.csv" out
  IO.println s!"wrote build/packet_golden.csv (64 vectors)"

  IO.FS.writeFile "build/xr_grid_entity_packet.h" cHeader
  IO.println s!"wrote build/xr_grid_entity_packet.h ({SIZE} bytes, payload at {PAYLOAD_OFFSET})"
