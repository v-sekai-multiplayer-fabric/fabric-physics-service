# interactor-ward

This repository is a server-authoritative networked physics service for one ward. It starts as
a clone of `datasource-queen` and still holds that repository's two processes: `queen`, a
settlement game, and the data source, SQLite on a VFS that keeps its pages in FoundationDB. The
two link into one process.

The clone is the point rather than a shortcut. The physics service needs a zone with a durable
ward, a 20 Hz publish, a fanout that slices, and a CI that runs against a live FoundationDB, and
all four exist here and run. What it does not have yet is the physics: authority over a moving
body, and the priority accumulator that decides which bodies a subscriber hears about. Those go
on top of the ward, not beside it.

`README.md` gives the design. The comments in `src/queen.c` give the reasons. Record decisions
in the `multiplayer-fabric-manuals` repository. `CITATION.cff` says what this repository is built
on — the design it implements, the code it clones and vendors, and the specifications its numbers
come from. Add a dependency there when you add one here.

## Build

The build needs a FoundationDB client, SQLite headers, CMake, and a C/C++ toolchain.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Set `WEFT_FDB_CLUSTER_FILE` to the cluster file before you run `queen`. A run without a live
cluster fails at start.

## Run

```sh
./build/queen play  200 20260811 8     # play one ward
./build/queen check 200 20260811 8     # play one ward twice, then hold it to its arithmetic
./build/queen shard  40 31337   60     # play a game of more than one ward
```

The arguments are cycles, seed, and Sparks, in that order.

## Checks

CI plays the game. A run that holds its invariants is the test.

Run the full CI on this machine in a container:

```sh
docker compose run --rm ci        # the whole of .github/workflows/ci.yml
docker compose run --rm shell     # the same container, cluster up, at a prompt
```

The Compose plugin is absent on some machines. On a machine with rootless podman, use the
quadlet in `ci/fabric-store-ci.container` instead.

CI MUST use a real FoundationDB, a real transport, and real certificates. Do not add a
test-only path. A door that only CI opens proves nothing about production.

## Limits

These constants bound the design. Read the value from the source. Do not copy a value into new
code.

| Constant           | Value | Where                          | What it bounds                        |
| ------------------ | ----- | ------------------------------ | ------------------------------------- |
| `SLICE_ENTITIES`   | 64    | `src/queen.c`                  | Entities in one subscriber slice      |
| `WARD_ENTITIES`    | 1800  | `src/ward.h`                   | Entities in one zone, from `AbyssalSLA.lean` |
| `WARD_HEADROOM`    | 400   | `src/ward.h`                   | Of those, the ghosts a neighbour replicates in |
| `SPARKS_PER_WARD`  | 1384  | `src/queen.c`                  | Sparks in one ward                    |
| `BOARD_SIZE`       | 6     | `src/queen.c`                  | Contracts on the board, plus 3 with the Rails |
| `MAX_WARDS`        | 8     | `src/queen.c`                  | Wards in one `shard` run              |
| `TXN_MAX_PARTS`    | 16    | `thirdparty/store-plane/fdb_vfs.c` | Databases in one group commit     |
| `WARD_TICK_HZ`     | 20    | `src/queen.c`                  | Publish rate, in real time            |
| `WARD_REPLY_MAX`   | 262144 | `src/interactor.h`            | Bytes in one interactor reply         |
| `PAGE`             | 4096  | `thirdparty/store-plane/fdb_vfs.c` | SQLite page size                  |

`WARD_REPLY_MAX` bounds `WARD_ENTITIES`. An `XRGridEntityPacket` is 100 bytes, so one reply
holds 2621 entities and a ward of 1800 is one whole snapshot with room over — no chunking and
no second transport. Past 2621 the answer is a chunked reply and not a larger buffer, because
`ward_ask` already refuses to cut a batch a reader cannot tell from a complete one.

A ward MUST NOT hold more Sparks than `SPARKS_PER_WARD`. The code refuses a larger ward. The
refusal is a feature. Past that limit, add a second ward.

The board bounds the work. At most 9 Sparks act in one cycle. A larger ward does not raise this
number.

## Invariants

Every change MUST keep these three properties. CI asserts all three.

1. Scrip is conserved. `treasury + purses + paid to the clock + built with == issued`.
2. Salvage is unique. No item is in two Sparks' hands.
3. One seed makes one ward. `check` plays a seed twice and compares a fingerprint.

A cycle MUST NOT advance on wall-clock time. Wall-clock time breaks the replay check.

A change MUST NOT alter the RNG draw order. A new draw moves every later number and changes
every seed's ward.

## Storage

The store keeps no local file. SQLite runs with a VFS whose pages live in FoundationDB.

Set `PRAGMA journal_mode=MEMORY`. This stops SQLite from writing a journal behind the VFS.

Do not open a database file on a disk. A local file makes storage stateful. Stateful storage
does not migrate.

## Transactions

A commit is one FoundationDB transaction. A commit costs about one round trip.

Group the writes of one cycle. Autocommit makes each statement its own round trip.

A payment spans two databases. Use the parallel commit protocol. Both sides MUST land, or
neither.

One group holds at most `TXN_MAX_PARTS` databases. `weft_txn_join` answers `SQLITE_FULL` past
that limit. Handle the refusal. Do not assume the limit holds.

## Wire format

Use bitpacked bytes on the hot path. Use JSON-LD as CBOR everywhere else.

Do not send plain JSON text.

WebTransport needs no framing layer. A datagram is one message. A stream FIN is the boundary.

Do not propose `webtransportd`. The Queen terminates QUIC in her own process.

## One transport

WebTransport is the only transport. Do NOT add a second one — not TCP, not raw UDP, not ENet,
not WebSocket — and do not restore the line-framed TCP server that used to sit beside it.

A service with two transports has two sets of framing rules and two things to keep in step with
every client. WebTransport supplies its own boundaries and the TCP server had to invent a length
prefix to make up for having none, so the two never agreed about what a message was.

`serve` MUST refuse to start without `QUEEN_TLS_CERT` and `QUEEN_TLS_KEY`. It used to fall back
to the plaintext TCP server when they were absent, which is the failure this rule exists to
prevent: a ward whose secret failed to mount would come up, answer, and look exactly like
success. A missing credential is a refusal, not a downgrade.

picoquic takes file paths rather than PEM buffers. A deployment holding the PEM in a secret
writes it to disk first; `fly/entrypoint.sh` does that.

## Vendored code

`thirdparty/` holds vendored subtrees.

| Path                       | Source                                        |
| -------------------------- | --------------------------------------------- |
| `thirdparty/store-plane`   | `datasource-store`                          |
| `thirdparty/gateway-edge`  | `transport-gateway-c`                         |
| `thirdparty/taskweft`      | `nif`, the `standalone/` headers              |
| `thirdparty/mujoco-riscv64`| `mujoco-riscv64`, MuJoCo 3.11.0 and its wiring |

Vendor only from a `v-sekai-multiplayer-fabric` repository. A vendored copy MUST be
byte-identical to its source. Send a fix upstream first. Then vendor the fix.

`thirdparty/mujoco-riscv64` is not an interactor and does not build here yet. It is
`mj_physics_init`/`mj_physics_step`/`mj_physics_close` over MuJoCo's C API, which loads an
in-memory MJCF scene and steps it, and nothing that reads ward state or writes it back. Making
it an interactor is `ask` in and reply bytes out over `weft_interactor_t`, with the ward as the
scene rather than the fixed test body. Read `thirdparty/mujoco-riscv64/README.md` for the scope
it claims; it is honest about what it has not done, and none of that is done by vendoring it.

The subtree carries all of MuJoCo, which is about 200 MB checked out. That is the cost of the
byte-identical rule and it is paid on every clone.

## Key files

| Path                                | Purpose                                       |
| ----------------------------------- | --------------------------------------------- |
| `src/queen.c`                       | The game, the ward, and the cycle             |
| `src/planner.cpp`                   | The Queen's HTN domain                        |
| `thirdparty/store-plane/fdb_vfs.c`  | The VFS, and the parallel commit protocol     |
| `thirdparty/store-plane/bench_vfs.c`| The VFS benchmark                             |
| `ci/inside.sh`                      | The CI steps, in order                        |
| `.github/workflows/ci.yml`          | The CI job                                    |
| `docker-compose.yml`                | CI in a container on a developer machine      |

## Conventions

- Do not use the word "mint" in this project. Check branch names also. A rename of a head
  branch closes its PR.
- Put a runbook in the unit file. Put a reason in a code comment. Put a decision in the
  manuals repository. `README.md` is not a manual.
- Print all of a list, or print a count and a note. Do not print part of a list.
- Do not hardcode an absolute filesystem path. Use an environment variable.
- Build out of the source tree. The host `build/` is not the container `build/`.
