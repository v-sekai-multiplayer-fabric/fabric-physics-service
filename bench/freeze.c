// Freeze on settle. See freeze.h for why.
//
// SPDX-License-Identifier: Apache-2.0
#include "freeze.h"

#include <stdlib.h>
#include <string.h>

weft_freeze_cfg weft_freeze_defaults(void) {
	weft_freeze_cfg c;
	c.still_speed = 0.01;
	c.still_ticks = 30;
	c.max_per_tick = 64;
	return c;
}

int weft_freeze_init(weft_freeze_t *f, const mjModel *m) {
	if (!f || !m) return -1;
	memset(f, 0, sizeof(*f));
	f->nbody = m->nbody;
	f->still_for = (int *)calloc((size_t)m->nbody, sizeof(int));
	return f->still_for ? 0 : -1;
}

void weft_freeze_close(weft_freeze_t *f) {
	if (!f) return;
	free(f->still_for);
	f->still_for = NULL;
	f->nbody = 0;
}

// The body's linear speed. `cvel` is spatial velocity in the body frame: the last three
// components are linear, which is what settling is about -- a body spinning in place is
// still moving and should not be promoted into scenery.
static mjtNum body_speed(const mjModel *m, const mjData *d, int b) {
	const mjtNum *v = d->cvel + 6 * b + 3;
	return mju_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Is this body a free-floating thing a player made, rather than part of something?
//
// Only bodies with exactly one free joint are candidates. A body on a hinge belongs to a
// mechanism, and a body with no joint is already static. This is also what keeps a
// skeleton out of it: its bones are on ball joints.
static int is_freezable(const mjModel *m, int b, const char *keep_prefix) {
	if (m->body_jntnum[b] != 1) return 0;
	if (m->jnt_type[m->body_jntadr[b]] != mjJNT_FREE) return 0;
	if (keep_prefix && *keep_prefix) {
		const char *nm = m->names + m->name_bodyadr[b];
		if (strncmp(nm, keep_prefix, strlen(keep_prefix)) == 0) return 0;
	}
	return 1;
}

int weft_freeze_tick(weft_freeze_t *f, mjSpec *spec, mjModel **m, mjData **d,
                     const weft_freeze_cfg *cfg, const char *keep_prefix) {
	if (!f || !spec || !m || !*m || !d || !*d || !cfg) return 0;
	mjModel *mm = *m;
	mjData *dd = *d;

	if (f->nbody != mm->nbody) {          // model changed under us; resize and skip a tick
		int *grown = (int *)calloc((size_t)mm->nbody, sizeof(int));
		if (!grown) return 0;
		free(f->still_for);
		f->still_for = grown;
		f->nbody = mm->nbody;
		return 0;
	}

	// Which bodies have held still long enough. Collected before any editing, because
	// mjs_delete invalidates ids and the pose has to be read from the model being replaced.
	int *ripe = (int *)malloc((size_t)cfg->max_per_tick * sizeof(int));
	if (!ripe) return 0;
	int n_ripe = 0;

	for (int b = 1; b < mm->nbody; b++) {
		if (!is_freezable(mm, b, keep_prefix)) {
			f->still_for[b] = 0;
			continue;
		}
		if (body_speed(mm, dd, b) < cfg->still_speed) {
			f->still_for[b]++;
		} else {
			f->still_for[b] = 0;
		}
	}

	// Freezing is a transfer of ownership -- a body leaves the simulation and joins the
	// world -- and the unit of transfer is whatever is internally coupled, exactly as it is
	// for a handoff between two zones. You cannot move half a stack across a boundary and
	// you cannot freeze half of one either: the settled half would become immovable while
	// the rest kept leaning on it, which is a different structure from the one that settled.
	//
	// MuJoCo already computes the grouping. `dof_island` is the constraint island a dof
	// belongs to, so bodies that are touching share one, and an island is ripe only when
	// every body in it is. Bodies in no island (-1) are alone and ripe on their own.
	for (int b = 1; b < mm->nbody && n_ripe < cfg->max_per_tick; b++) {
		if (f->still_for[b] < cfg->still_ticks) continue;

		const int isl = (mm->body_dofnum[b] > 0 && dd->dof_island)
		              ? dd->dof_island[mm->body_dofadr[b]] : -1;

		if (isl < 0) {                       // touching nothing: transfers alone
			ripe[n_ripe++] = b;
			continue;
		}

		int whole_island_ready = 1;          // every body sharing this island must be still
		for (int o = 1; o < mm->nbody; o++) {
			if (mm->body_dofnum[o] <= 0) continue;
			if (dd->dof_island[mm->body_dofadr[o]] != isl) continue;
			if (!is_freezable(mm, o, keep_prefix) ||
			    f->still_for[o] < cfg->still_ticks) {
				whole_island_ready = 0;
				break;
			}
		}
		if (!whole_island_ready) continue;

		for (int o = 1; o < mm->nbody && n_ripe < cfg->max_per_tick; o++) {
			if (mm->body_dofnum[o] <= 0) continue;
			if (dd->dof_island[mm->body_dofadr[o]] != isl) continue;
			int already = 0;
			for (int k = 0; k < n_ripe; k++) if (ripe[k] == o) { already = 1; break; }
			if (!already) ripe[n_ripe++] = o;
		}
	}

	if (!n_ripe) {
		free(ripe);
		return 0;
	}

	// Promote. For each ripe body: copy its geoms onto the worldbody at the pose the body
	// reached, then delete the body. Reading the pose from `xpos`/`xquat` rather than from
	// the spec is the point -- the spec still holds where the thing was authored, and what
	// should persist is where it came to rest.
	mjsBody *world = mjs_findBody(spec, "world");
	if (!world) {
		free(ripe);
		return 0;
	}

	int frozen = 0;
	for (int i = 0; i < n_ripe; i++) {
		const int b = ripe[i];
		const char *nm = mm->names + mm->name_bodyadr[b];
		mjsBody *src = mjs_findBody(spec, nm);
		if (!src) continue;

		const mjtNum *bp = dd->xpos + 3 * b;
		const mjtNum *bq = dd->xquat + 4 * b;

		for (int g = 0; g < mm->body_geomnum[b]; g++) {
			const int gi = mm->body_geomadr[b] + g;
			mjsGeom *ng = mjs_addGeom(world, NULL);
			if (!ng) continue;
			ng->type = (mjtGeom)mm->geom_type[gi];
			for (int k = 0; k < 3; k++) ng->size[k] = mm->geom_size[3 * gi + k];

			// Geom pose is relative to its body; compose it onto the body's world pose.
			mjtNum gp[3], gq[4];
			mju_rotVecQuat(gp, mm->geom_pos + 3 * gi, bq);
			mju_addTo3(gp, bp);
			mju_mulQuat(gq, bq, mm->geom_quat + 4 * gi);
			for (int k = 0; k < 3; k++) ng->pos[k] = gp[k];
			for (int k = 0; k < 4; k++) ng->quat[k] = gq[k];

			for (int k = 0; k < 3; k++) ng->friction[k] = mm->geom_friction[3 * gi + k];
			for (int k = 0; k < 4; k++) ng->rgba[k] = mm->geom_rgba[4 * gi + k];
			ng->condim = mm->geom_condim[gi];
			ng->contype = mm->geom_contype[gi];
			ng->conaffinity = mm->geom_conaffinity[gi];
		}

		if (mjs_delete(spec, src->element) == 0) frozen++;
	}
	free(ripe);

	if (!frozen) return 0;

	// mj_recompile carries mjData across the rebuild, so everything still moving keeps its
	// velocity. Without it a freeze would visibly jolt the rest of the scene, which is the
	// sort of thing that makes a mechanism unshippable however good its numbers are.
	if (mj_recompile(spec, NULL, mm, dd) != 0) return 0;

	f->frozen_total += frozen;
	f->recompiles++;

	if (f->nbody != mm->nbody) {
		int *grown = (int *)calloc((size_t)mm->nbody, sizeof(int));
		if (grown) {
			free(f->still_for);
			f->still_for = grown;
			f->nbody = mm->nbody;
		}
	}
	return frozen;
}
