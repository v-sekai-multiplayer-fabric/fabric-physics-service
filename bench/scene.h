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

// How tall a cube is, in metres. The geom is a box of half-extent 0.2, so a tower of fifty is
// twenty metres — which is the article's own "huge stacks of cubes, up to 20 or 30 meters high".
#define CUBE_METRES 0.4

// An MJCF scene of `cubes` simulated boxes and `players` avatars, on a floor. The caller frees
// it, and gets NULL if the arguments do not make a scene.
//
// Entities are `cubes + players * AVATAR_ENTITIES`, in that order: the cubes are the world and
// the avatars are the people in it, which is also the order `bench_players` encodes them.
//
// `stack` is how many cubes high to pile them. Zero is a flat field, one cube deep, which is
// what a zone looks like before anybody has played in it. Anything more builds towers, which is
// what the article's players actually make and a much harder contact problem: a settled grid
// touches the floor and nothing else, while a tower of fifty is fifty contacts in a chain that
// the solver has to keep upright every step. Measuring only the field understates the simulate
// stage and flatters determinism, because the easy case is also the reproducible one.
char *gaffer_scene(int players, int cubes, int stack, double timestep);

#endif
