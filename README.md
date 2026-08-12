# fabric-fanout-edge

One tick of a zone, filtered by interest and sent to the subscribers who should see it.

An **edge plane** is a plane with networking, and this is the egress one. The gateway and
ingest edges terminate client transport on the way *in*; this one is the way out, and it is
a different job: driven by the zone tick rather than by an arriving packet, and doing work
per subscriber rather than per connection.

## Two rules it exists to keep

**Never CBOR the entity packet.** The nasty path is millions of packets a second, so it is
the fixed 100-byte `XRGridEntityPacket`: memcpy and offset reads, no framing, no
self-description, version pinned per connection. The cheap path — join, auth, interest
changes — can afford validation, and does not run per tick.

**Batch by division, not delimiters.** A slice is back-to-back 100-byte records and the
receiver recovers the count as `len / 100`. One write per subscriber per tick.

Both come from [zguide ch.7](https://zguide.zeromq.org/docs/chapter7/), and both are why
this is a separate process from the plane that produces the state: the filtering is per
subscriber, and a single writer has no subscribers.

## The seam that is not wired

WebTransport is not in the container yet, so delivery goes through a `fanout_sink_t`
function pointer. Swapping the default sink for a WebTransport datagram sink changes nothing
above it, which is the point of it being a pointer.

## The domain it belongs to

It reads entity state from `fabric-authority-plane` every tick. A ring forces co-location,
so those two share a machine. `fabric-asset-edge` does not.

## State

**Buildable, not deployed.** `src/fanout.cpp` is the interest filter and the packing, carried
over from `gyreplane` unchanged. Both generated headers it needs now exist —
`predictive_bvh.h` in `lean-spatial-oracle` and `xr_grid_entity_packet.h` emitted by
`lean-entity-packet`'s `packet_emit` — so pointing `WEFT_GEN_DIR` at a directory holding
`predictive_bvh.h` and `gen/xr_grid_entity_packet.h` compiles this. It had never compiled
before, because the packet codec was a file everything included and nobody had emitted.

One thing a caller must still supply: `aabb_overlaps`. `predictive_bvh.h` declares it and says
its definition lives in the adapter, so a caller with no Godot in it writes the six comparisons
itself.

It is not a process yet: there is no loop and no transport, only the logic a loop would call,
and it needs the ring subscription that feeds it a tick. A caller with neither can still reach
the real filtering — `fanout_sink_t` takes a counter as happily as a socket, which is how
`fabric-physics-service`'s one-core benchmark times this without a network.
