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
// `iters` and `ls_iters` cap the solver: the maximum main iterations and linesearch iterations
// MuJoCo may spend on one step. Zero leaves MuJoCo's defaults, which are 100 and 50.
//
// This is the knob a real-time zone actually needs, and it is a deadline rather than a quality
// setting. An unbounded solver converges as far as the problem demands, so a collapsing tower
// costs whatever it costs — measured here at 1840 ms in a 50 ms tick. A capped solver returns a
// less-converged answer in bounded time, which is the trade a zone wants: physics that is
// slightly wrong beats physics that arrives after the snapshot went out.
//
// Capping does not cost determinism. Fewer iterations is still the same arithmetic in the same
// order, so a capped run replays exactly like an uncapped one — but the cap becomes part of the
// wire contract, like the simulation rate. Two peers solving to different iteration counts are
// in different worlds, and they diverge in the tick where the cap first bites.
// `pile` builds pyramids `pile` layers tall instead of a field or towers, and it exists to fail.
//
// It is the reproduction case for the limits work, not a scene to benchmark with. A pyramid
// stands because every cube rests on others, which is the arrangement with the most contacts
// there is: measured, 100 cubes make 1345 contacts and run at 0.92x realtime, 400 make 6435 and
// run at 0.02x. Neither a solver-iteration cap nor MuJoCo's sleep flag moves those numbers.
//
// So it is kept deliberately and named so it cannot be reached by accident. Anything that claims
// to bound the tick — welding settled assemblies, a wall-clock time-box — has to be shown against
// this, and a bound demonstrated only on the flat field is a bound demonstrated on the easy case.
//
// `stack` is the other failure and a different one: a column of `stack` cubes is that aspect
// ratio to one, and at fifty it does not topple but ejects, throwing its top cube from 19.9 m to
// 23 m in two seconds. Also kept, for the same reason.
//
// The scene to measure with is the flat field, `stack` and `pile` both zero. It holds 4.15x
// realtime at 900 cubes.
char *gaffer_scene(int players, int cubes, int stack, int pile, int iters, int ls_iters,
                   double timestep);

#endif
