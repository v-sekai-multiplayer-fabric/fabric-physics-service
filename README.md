# service-physics

A server-authoritative networked physics service for one ward, after
[networked physics in virtual reality](https://gafferongames.com/post/networked_physics_in_virtual_reality/)
with the authority moved to the server. The physics is not written yet.

## What is server-authoritative about it

Gaffer's design gives authority to whichever client last touched a cube, and every client runs
the same deterministic simulation so the hand-off agrees. That works because there is no server
in it. Here there is one, and it already owns the state a client would have to be trusted with:
a ward is a SQLite database over the store's VFS, a cycle is a transaction, and no client
opens a database. So authority does not move. What is left of the design is the part that was
never about trust — the priority accumulator that decides which of a ward's bodies a given
subscriber hears about this tick, which is the same problem `fanout_one` already has.

The sizes are already here and they agree with the shape:

| what | value | where |
| --- | --- | --- |
| entities in a ward | 1800, of which 400 are ghosts | `WARD_ENTITIES`, `WARD_HEADROOM` in `src/ward.h` |
| entities in one subscriber's tick | 64 | `SLICE_ENTITIES` in `src/queen.c` |
| publish rate | 20 Hz | `WARD_TICK_HZ` in `src/queen.c` |
| entities in one interactor reply | 2621 | `WARD_REPLY_MAX` / 100, `src/interactor.h` |

A ward does not fit in a slice and is not meant to: 1800 bodies at 100 bytes and 20 Hz is
3.6 MB/s, and 64 of them is 128 kB/s, which is the budget the accumulator is spending. A ward
does fit in one interactor reply, with 821 entities to spare, so the reliable path needs no
chunking. Those two facts are the whole of the transport decision, and both are read from the
source rather than copied into it.

## Where the durable state is described

This repository is a clone of [`service-store`](https://github.com/v-sekai-multiplayer-fabric/service-store),
and its README carried a copy of that repository's own documentation: the ring, the VFS, the
`queen` tenant, CI, and state. That copy is gone. `service-store` is the one place those are
written, and its `docs/design.md` holds them.
