# mujoco-riscv64

A riscv64 Linux build of [MuJoCo](https://github.com/google-deepmind/mujoco),
with host bindings for driving it as a `libriscv` sandboxed guest --
seeded from `v-sekai-multiplayer-fabric/zone-server-h2o`'s own vendored
MuJoCo work, moved here after that project dropped its own MuJoCo
dependency in favor of Godot's Jolt physics for entity/prop contact
physics.

## What is here

- `thirdparty/mujoco`: real MuJoCo source, pinned to commit
  `b85fdca54f0e0038b804af146a0b4e94199e00d0` (tag `3.11.0`), a clean
  checkout of that exact commit (not the `zone-server-h2o` repo's own
  git history, which briefly held a corrupted subtree merge -- see
  Provenance below).
- `cmake/mujoco.cmake`: the CMake vendoring recipe that adds MuJoCo as
  a subdirectory target, carried over from `zone-server-h2o`.
- `src/physics/mj_physics.c`/`.h`: the real MuJoCo C API wiring
  (`mj_physics_init`/`mj_physics_step`/`mj_physics_close`) built and
  linked in `zone-server-h2o`, ported here as-is.
- `test/unit/test_mj_physics_freefall.c`: the one MuJoCo test that
  existed, a free-fall recovery check against a minimal MJCF scene.

## Scope

This repo does not yet cross-compile MuJoCo for riscv64 or wire it
into a `libriscv` guest -- that is real, unstarted work, not done by
this initial seed commit. What exists today is x86_64-buildable
(confirmed: this exact `mj_physics.c` + `cmake/mujoco.cmake` pairing
built and linked clean in `zone-server-h2o`'s own real containerized
CI, multiple times, before being moved here).

## Provenance

`zone-server-h2o` vendored MuJoCo as a git submodule, pinned at
`b85fdca54f0e0038b804af146a0b4e94199e00d0`. An attempt to convert it
to a `git subtree` was interrupted mid-fetch and produced a corrupted
squash commit (its "MuJoCo" tree was actually a copy of
`zone-server-h2o`'s own repo, not MuJoCo). That corrupted commit was
never pushed anywhere as the real MuJoCo content -- this repo's own
`thirdparty/mujoco` is a fresh, verified checkout of the same pinned
commit directly from MuJoCo's own upstream, confirmed by its real
Apache license header and README banner, not carried over from that
corrupted merge.
