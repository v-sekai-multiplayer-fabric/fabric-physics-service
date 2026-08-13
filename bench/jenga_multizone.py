"""Jenga, and what happens when a tower is split across two zones.

A Jenga tower is the hardest stability case a sandbox produces on purpose: eighteen
levels of three blocks, every level rotated ninety degrees, the whole thing standing
on friction and nothing else. It is also one contact island from top to bottom, which
makes it the sharpest possible test of a question this workspace keeps circling --
what does a zone boundary do to a body that spans it?

Four things measured:

  1. Does the tower stand at all, and what does it cost per tick.
  2. Pull a block. Does it fall, and what does the collapse cost.
  3. Split it at level 9 into two zones, each treating the other half as static ghosts,
     and compare against the whole-tower ground truth. This is the naive multizone and
     the divergence is the finding.
  4. Where the split can be put so the divergence is tolerable, if anywhere.

Run:  python bench/jenga_multizone.py
Needs the same pinned MuJoCo the rest of the bench uses: pip install mujoco==3.11.0

SPDX-License-Identifier: Apache-2.0
"""
import math
import statistics as st
import time

import mujoco
import numpy as np

# Real Jenga: 75 x 25 x 15 mm. Half-extents, in metres.
HX, HY, HZ = 0.0375, 0.0125, 0.0075
LEVELS = 18
PER_LEVEL = 3


def block_positions():
    """(level, index, x, y, z, rotated) for every block in a full tower."""
    out = []
    for lvl in range(LEVELS):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            off = (i - 1) * (HY * 2)
            x, y = (off, 0.0) if rot else (0.0, off)
            out.append((lvl, i, x, y, z, rot))
    return out


def tower(pull=None, static_below=None, static_above=None, solver_iters=0):
    """A Jenga tower.

    `pull` removes one (level, index), which is the move.
    `static_below` / `static_above` make blocks outside a level range into worldbody
    geoms -- the naive ghost: present for collision, not simulated, cannot move.
    """
    p = ['<mujoco><option timestep="0.002"']
    if solver_iters:
        p.append(f' iterations="{solver_iters}"')
    p.append('><flag sleep="enable"/></option>')
    p.append('<default><geom friction="0.6 0.005 0.0001"/></default>')
    p.append('<worldbody><geom name="floor" type="plane" size="5 5 0.1"/>')
    for lvl, i, x, y, z, rot in block_positions():
        if pull is not None and (lvl, i) == pull:
            continue
        q = 'quat="0.7071 0 0 0.7071"' if rot else ''
        frozen = ((static_below is not None and lvl < static_below) or
                  (static_above is not None and lvl >= static_above))
        g = (f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" {q}/>')
        if frozen:
            p.append(f'<geom type="box" size="{HX} {HY} {HZ}" pos="{x:g} {y:g} {z:g}" {q}/>')
        else:
            p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>{g}</body>')
    return ''.join(p) + '</worldbody></mujoco>'


def run(xml, steps, record=False):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    t0 = time.perf_counter()
    for _ in range(steps):
        mujoco.mj_step(m, d)
    ms_tick = (time.perf_counter() - t0) / steps * 1000.0 * 25  # 2 ms substep, 50 ms tick
    pos = d.xpos[1:].copy() if record else None
    return ms_tick, d, m, pos


def top_height(d, m):
    return float(d.xpos[1:, 2].max()) if m.nbody > 1 else 0.0


def standing(d, m):
    """Still a tower? Top block within a block-height of where it started."""
    return abs(top_height(d, m) - (HZ + (LEVELS - 1) * HZ * 2)) < HZ * 3


print(f'MuJoCo {mujoco.__version__} -- Jenga, {LEVELS} levels x {PER_LEVEL}, '
      f'{LEVELS*PER_LEVEL} blocks, 2 ms substep')
print()

# ── 1. does it stand ────────────────────────────────────────────────────────────
print('1. DOES IT STAND')
for steps, label in ((250, '0.5 s'), (1000, '2 s'), (2500, '5 s')):
    ms, d, m, _ = run(tower(), steps)
    print(f'   after {label:>5}: top at {top_height(d, m)*100:6.2f} cm  '
          f'{"STANDING" if standing(d, m) else "FALLEN":9} {ms:7.2f} ms/tick  ncon={d.ncon}')

# ── 2. pull a block ─────────────────────────────────────────────────────────────
print()
print('2. PULL ONE BLOCK (the move)')
for lvl, idx, note in ((6, 1, 'middle block, low'), (12, 1, 'middle block, high'),
                       (6, 0, 'edge block, low')):
    ms, d, m, _ = run(tower(pull=(lvl, idx)), 1500)
    print(f'   pull level {lvl:2d} idx {idx} ({note:20}) -> '
          f'{"STANDS" if standing(d, m) else "COLLAPSES":10} {ms:7.2f} ms/tick')

# ── 3. split across two zones ───────────────────────────────────────────────────
print()
print('3. SPLIT ACROSS TWO ZONES, each ghosting the other half as static')
STEPS = 500
_, dg, mg, truth = run(tower(), STEPS, record=True)
print(f'   ground truth: whole tower, {STEPS} steps, top {top_height(dg,mg)*100:.2f} cm')

for cut in (3, 6, 9, 12, 15):
    # zone A owns levels [0, cut), ghosts the rest
    _, da, ma, pa = run(tower(static_above=cut), STEPS, record=True)
    # zone B owns levels [cut, LEVELS), ghosts the rest
    _, db, mb, pb = run(tower(static_below=cut), STEPS, record=True)
    n_a = cut * PER_LEVEL
    stitched = np.vstack([pa[:n_a], pb[:len(truth) - n_a]])
    drift = np.linalg.norm(stitched - truth, axis=1)
    print(f'   cut at level {cut:2d}: mean drift {drift.mean()*1000:8.2f} mm   '
          f'max {drift.max()*1000:9.2f} mm   '
          f'{"agrees" if drift.max() < HZ * 2 else "DIVERGED"}')

print()
print(f'   (a block is {HZ*2*1000:.0f} mm tall; the threshold is one full block --')
print('    jenga_torture.py uses the same one, so the verdicts agree)')

# ── 4. split a COLLAPSING tower ─────────────────────────────────────────────────
print()
print('4. SPLIT A COLLAPSING TOWER (pull the edge block, then cut)')
print('   the standing case above splits cleanly because it is asleep -- nothing that')
print('   was frozen was moving. This is the case a seam actually has to survive.')
PULL = (6, 0)   # the edge block that collapses it
_, dg2, mg2, truth2 = run(tower(pull=PULL), STEPS, record=True)
print(f'   ground truth: whole tower collapsing, top {top_height(dg2,mg2)*100:.2f} cm, '
      f'ncon={dg2.ncon}')
for cut in (3, 6, 9, 12, 15):
    _, _, _, pa = run(tower(pull=PULL, static_above=cut), STEPS, record=True)
    _, _, _, pb = run(tower(pull=PULL, static_below=cut), STEPS, record=True)
    n_a = cut * PER_LEVEL - (1 if PULL[0] < cut else 0)
    stitched = np.vstack([pa[:n_a], pb[:len(truth2) - n_a]])
    drift = np.linalg.norm(stitched - truth2, axis=1)
    verdict = 'agrees' if drift.max() < HZ * 2 else 'DIVERGED'
    print(f'   cut at level {cut:2d}: mean drift {drift.mean()*1000:8.2f} mm   '
          f'max {drift.max()*1000:9.2f} mm   {verdict}')
