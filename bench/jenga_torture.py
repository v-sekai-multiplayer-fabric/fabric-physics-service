"""Jenga, split every awkward way a zone boundary can be drawn.

`jenga_multizone.py` cuts a tower once, horizontally, between two levels -- the kindest
possible seam. This is the unkind version:

  1. N-way slicing. Two zones, three, six, nine, eighteen. Does drift grow with the
     number of seams, or with the amount of motion crossing them?
  2. A vertical plane cut, which slices THROUGH levels rather than between them, so a
     single contact pair straddles the boundary.
  3. Both at once -- a 3D zone grid, which is what a real spatial partition looks like.
  4. Every one of those again while the tower is collapsing.

The ghost model is the naive one throughout: a block outside a zone is a static geom at
its start position, with no velocity and no handoff. A real implementation would carry
ghost velocities and do better. This measures the floor, not the ceiling.

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco
import numpy as np

from human_scale import like

HX, HY, HZ = 0.0375, 0.0125, 0.0075
LEVELS, PER_LEVEL = 18, 3
STEPS = 500
PULL = (6, 0)          # the edge block whose removal collapses the tower


def blocks():
    out = []
    for lvl in range(LEVELS):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            off = (i - 1) * (HY * 2)
            x, y = (off, 0.0) if rot else (0.0, off)
            out.append((lvl, i, x, y, z, rot))
    return out


ALL = blocks()


def build(owned, pull=None):
    """`owned` is a predicate over (lvl, i, x, y, z). True = simulated here."""
    p = ['<mujoco><option timestep="0.002"><flag sleep="enable"/></option>',
         '<default><geom friction="0.6 0.005 0.0001"/></default>',
         '<worldbody><geom name="floor" type="plane" size="5 5 0.1"/>']
    for lvl, i, x, y, z, rot in ALL:
        if pull is not None and (lvl, i) == pull:
            continue
        q = 'quat="0.7071 0 0 0.7071"' if rot else ''
        body = f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" {q}/>'
        if owned(lvl, i, x, y, z):
            p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>{body}</body>')
        else:
            p.append(f'<geom type="box" size="{HX} {HY} {HZ}" pos="{x:g} {y:g} {z:g}" {q}/>')
    return ''.join(p) + '</worldbody></mujoco>'


def sim(xml):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    for _ in range(STEPS):
        mujoco.mj_step(m, d)
    return d.xpos[1:].copy()


def order(pull):
    """Block identities in model order, so a zone's output can be indexed."""
    return [(l, i) for (l, i, *_ ) in ALL if pull is None or (l, i) != pull]


def stitch(zones, pull):
    """Run each zone, take from each only the blocks it owns, assemble one tower."""
    ids = order(pull)
    out = np.zeros((len(ids), 3))
    for owned in zones:
        pos = sim(build(owned, pull))
        k = 0
        for n, (l, i) in enumerate(ids):
            lvl, idx, x, y, z, rot = next(b for b in ALL if b[0] == l and b[1] == i)
            if owned(l, i, x, y, z):
                out[n] = pos[k]
                k += 1
            else:
                pass
        # non-owned blocks are static geoms and do not appear in xpos ordering;
        # recount owned-only indices
    return out, ids


def stitch_correct(zones, pull):
    """As above, but indexing owned blocks properly: static geoms carry no body row."""
    ids = order(pull)
    out = np.full((len(ids), 3), np.nan)
    for owned in zones:
        pos = sim(build(owned, pull))
        k = 0
        for n, (l, i) in enumerate(ids):
            b = next(bb for bb in ALL if bb[0] == l and bb[1] == i)
            if owned(l, i, b[2], b[3], b[4]):
                out[n] = pos[k]
                k += 1
    return out, ids


def report(label, zones, pull, truth):
    got, _ = stitch_correct(zones, pull)
    assert not np.isnan(got).any(), 'a block was owned by no zone'
    drift = np.linalg.norm(got - truth, axis=1)
    flag = 'agrees' if drift.max() < HZ * 2 else 'DIVERGED'
    mx = drift.max()*1000
    print(f'   {label:<34} mean {drift.mean()*1000:7.2f} mm   '
          f'max {mx:8.2f} mm  {flag:9} {like(mx)}')
    return drift.max()


def slabs(n):
    """n horizontal slabs of levels."""
    per = LEVELS / n
    return [(lambda lo, hi: (lambda l, i, x, y, z: lo <= l < hi))(int(k*per), int((k+1)*per))
            for k in range(n)]


def plane_x():
    """A vertical plane at x=0. Slices rotated levels through the middle."""
    return [lambda l, i, x, y, z: x < 0.0,
            lambda l, i, x, y, z: x >= 0.0]


def grid_3d(n_slab):
    """Slabs crossed with the vertical plane: a 3D partition."""
    out = []
    per = LEVELS / n_slab
    for k in range(n_slab):
        lo, hi = int(k*per), int((k+1)*per)
        out.append((lambda lo, hi: (lambda l, i, x, y, z: lo <= l < hi and x < 0.0))(lo, hi))
        out.append((lambda lo, hi: (lambda l, i, x, y, z: lo <= l < hi and x >= 0.0))(lo, hi))
    return out


print(f'MuJoCo {mujoco.__version__} -- Jenga torture, {LEVELS*PER_LEVEL} blocks, '
      f'{STEPS} steps, naive static ghosts')
print(f'a block is {HZ*2*1000:.0f} mm tall; "diverged" is max drift over {HZ*2*1000:.0f} mm')

for pull, what in ((None, 'SETTLED TOWER'), (PULL, 'COLLAPSING TOWER')):
    truth = sim(build(lambda *a: True, pull))
    print(f'\n=== {what}')
    for n in (2, 3, 6, 9, 18):
        report(f'{n:2d} horizontal slabs ({n-1} seams)', slabs(n), pull, truth)
    report('vertical plane at x=0', plane_x(), pull, truth)
    for n in (2, 3, 6):
        report(f'3D grid: {n} slabs x 2 halves', grid_3d(n), pull, truth)
