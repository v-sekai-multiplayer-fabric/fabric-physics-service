// The article's scene, as MJCF.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef BENCH_SCENE_H
#define BENCH_SCENE_H

// How many entities one player is. The article's own answer: "avatars are represented by a head
// and two hands driven by the tracked headset and touch controller positions and orientations."
//
// It is three and not a joint count. A per-joint avatar would spend the zone's whole entity
// budget on articulation nothing here has asked for, and the budget is the scarce thing.
#define AVATAR_ENTITIES 3

// An MJCF scene of `cubes` simulated boxes and `players` avatars, on a floor. The caller frees
// it, and gets NULL if the arguments do not make a scene.
//
// Entities are `cubes + players * AVATAR_ENTITIES`, in that order: the cubes are the world and
// the avatars are the people in it, which is also the order `bench_players` encodes them.
char *gaffer_scene(int players, int cubes, double timestep);

#endif
