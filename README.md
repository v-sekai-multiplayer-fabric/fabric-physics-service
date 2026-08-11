# fabric-store-domain

Durable state. A **domain** is a packing of planes and edge planes, and this one is packed by
what a caller can reach and what it cannot.

| member | what it does | where it runs |
| --- | --- | --- |
| `fabric-store-plane` | SQLite over a VFS whose pages live in FoundationDB, on rivet's Depot layout | on its caller's machine |
| FoundationDB | the pages, and every durable transaction. Below the planes, not one of them. | anywhere |
| `versitygw` | the S3-compatible endpoint FoundationDB backs up to with `fdbbackup` | with FoundationDB |

## What needs a ring, and what does not

A caller reaches the store plane over iceoryx2, and **iceoryx2 is shared memory**. So the
plane sits on the machine its caller sits on, and it is not a thing to deploy on its own.
`fly/fly.toml` in `fabric-store-plane` says the same from the other side: a second Fly app
cannot be the other end of a ring.

What needs no ring is **FoundationDB**. A commit is one FoundationDB transaction, about 1 ms,
and everything reaching the store already pays that, so the pages may be their own machine,
their own region, or several. That is the part of this domain that deploys by itself, and
`fabric-zone-domain` does not care where it lands.

This file used to say the whole domain needed no ring, and so that the store plane could live
anywhere. That was true of the pages and false of the plane, and the two are not the same
thing. See `fabric-store-plane#15`.

## Why the store holds no local file

SQLite runs with a VFS whose pages are in FoundationDB, so there is no database file on any
disk, and an actor's database moves between machines with no copy and no restore.
`PRAGMA journal_mode=MEMORY` stops SQLite writing one behind the VFS. rivet lists the same rule
as binding, for the same reason: a local file makes storage stateful and not migratable.

## What it is not

It is not two domains. SQLite and FoundationDB are not separable here, because the VFS is the
bridge between them: splitting them would put a ring hop inside a page read, and a page read is
already one FoundationDB round trip. FoundationDB is a database the store plane is a client
of. It sits below the planes rather than among them, so it is not a member the way a plane is,
and calling it a service would put it in the same word as a plane and a domain.

## State

**Not built.** This holds the packing. The Fly machine definition for FoundationDB and
versitygw comes next, and that is the only part of this with a machine of its own: the plane
ships with whatever calls it. `fabric-store-plane#17` tracks it.

One plane that runs on somebody else's machine, and the rest below the planes, is a thin thing
to call a domain, since a domain is a group of planes that share a ring. Whether this stays a
domain or becomes the store plane plus the storage under it is `fabric-store-plane#19`.
