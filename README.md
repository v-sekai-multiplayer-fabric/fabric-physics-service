# fabric-store-domain

Durable state, as one deployable thing. A **domain** is a packing of planes and edge planes,
and this is the packing that needs no ring.

| member | what it does |
| --- | --- |
| `fabric-store-plane` | SQLite over a VFS whose pages live in FoundationDB, on rivet's Depot layout |
| FoundationDB | the pages, and every durable transaction. A service, not a plane. |
| `versitygw` | the S3-compatible endpoint FoundationDB backs up to with `fdbbackup` |

## Why it is a domain of its own

**A ring forces co-location, and this needs no ring.** A commit costs one FoundationDB
transaction, about 1 ms, and everything reaching the store already pays that. So the store may
be its own machine, its own region, or several, and `fabric-zone-domain` does not care where
it lands.

That is the same rule that packs everything else together, read the other way: a member sits
with what it shares a ring with, and this shares a ring with nothing.

## Why the store holds no local file

SQLite runs with a VFS whose pages are in FoundationDB, so there is no database file on any
disk, and an actor's database moves between machines with no copy and no restore.
`PRAGMA journal_mode=MEMORY` stops SQLite writing one behind the VFS. rivet lists the same rule
as binding, for the same reason: a local file makes storage stateful and not migratable.

## What it is not

It is not two domains. SQLite and FoundationDB are not separable here, because the VFS is the
bridge between them: splitting them would put a ring hop inside a page read, and a page read is
already one FoundationDB round trip. FoundationDB is a database service that the store plane is
a client of, and not a plane.

## State

**Not built.** This holds the packing. The quadlet units and the Fly machine definition come
next.
