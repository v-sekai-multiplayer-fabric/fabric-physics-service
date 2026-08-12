# entity_packet

The Lean 4 + Plausible source-of-truth for the fabric's 100-byte entity packet
(`XRGridEntityPacket`). The wire is fully integral — no floats — so it models
exactly in Lean, and Plausible roundtrip properties find codec gaps in seconds
instead of an engine rebuild.

## Layout (100 bytes, all integral)

| offset | field | encoding |
| --- | --- | --- |
| 0 | global_id | u32 |
| 4 | position x/y/z | **int64 absolute micrometers** (no origin shift) |
| 28 | velocity x/y/z | i16, scaled to ±`PBVH_V_MAX_PHYSICAL_DEFAULT` (500000 μm/tick) |
| 40 | hlc | u32 (frame<<8 \| counter) |
| 44 | class\|owner | u32 |
| 48 | sub_index | u32 |
| 52 | rotation | i16 swing-twist ×3 |
| 58 | payload | 42 bytes userdata (cmd/action/state/name) |

Position int64 μm is the integral twin of the `precision=double` large-world
coordinate, and matches the Lean-proved predictive BVH's int64-μm AABB space
(`lean-predictive-bvh`, kept in sync). Velocity shares the BVH's `V_MAX` scale.

## The C codec

`xr_grid_entity_packet.h` is emitted from `EntityPacket/Codec.lean` and committed at the root,
so a repository that vendors this one gets the codec without running Lean. It is plain C with
no dependency beyond `stdint.h`: `xr_grid_entity_packet_t`, an encode, a decode, and the
offsets as macros.

It exists because `fabric-fanout-edge/src/fanout.cpp` has included it since it was written and
nobody had emitted it — its `CMakeLists.txt` names the missing header as the reason that edge
has never built, and says why copying one in by hand would be wrong: "copying the generated
headers here would put one decision in two places."

Do not edit it. Edit `Codec.lean`, where the offsets are named once and read by the encoder, the
decoder and the emitter, then run `packet_emit` again.

## Verify

```sh
lake exe packet_demo    # Plausible roundtrip + size, 50000-vector sweep
lake exe packet_emit    # writes packet_golden.csv and xr_grid_entity_packet.h
diff packet_golden.csv build/packet_golden.csv          # the committed copies are current
diff xr_grid_entity_packet.h build/xr_grid_entity_packet.h

# differential: the C the emitter wrote must produce the Lean bytes
cc -std=c11 -I build test/golden.c -o build/golden
./build/golden build/packet_golden.csv     # PACKET DIFFERENTIAL PASS

# differential: the engine's C++ XRGridEntityPacket.decode must match
godot --headless --script packet_diff.gd   # PACKET DIFFERENTIAL PASS
```

Verified: Plausible clean + 50000/50000 roundtrip; C++ decode matches the spec on 64 golden
vectors; the emitted C encoder matches all 64 byte for byte.
