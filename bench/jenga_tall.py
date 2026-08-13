"""A tall Jenga tower across many zones, with the pull done as an avatar motion.

Everything before this deleted a block to make the tower fall, which is not what a
player does. A player's hands are mocap bodies -- no degrees of freedom, driven from
outside every tick, colliding with everything. So here the block being removed IS a
mocap body, slid out over time by a scripted motion, exactly as `scene.c` drives an
avatar's hands. The tower has to survive the extraction rather than a deletion.

The tower is also much taller than eighteen levels, and cut into many zones, because
the question is whether a tall structure can stay stable while spanning boundaries.

Measured:
  1. How tall can it stand at all, before it falls over on its own.
  2. A mocap extraction: does the tower survive a slow pull, a fast one.
  3. The same, split into N zones, against a whole-tower ground truth.

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco
import numpy as np

HX, HY, HZ = 0.0375, 0.0125, 0.0075
PER_LEVEL = 3
SUBSTEP = 0.002


def blocks(levels):
    out = []
    for lvl in range(levels):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            off = (i - 1) * (HY * 2)
            x, y = (off, 0.0) if rot else (0.0, off)
            out.append((lvl, i, x, y, z, rot))
    return out


def build(levels, mocap=None, owned=None):
    """`mocap` is the (lvl, i) driven from outside -- the avatar's hand holding a block.
    `owned` decides which blocks this zone simulates; the rest are static ghosts."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"><flag sleep="enable"/></option>',
         '<default><geom friction="0.6 0.005 0.0001"/></default>',
         '<worldbody><geom name="floor" type="plane" size="5 5 0.1"/>']
    for lvl, i, x, y, z, rot in blocks(levels):
        q = 'quat="0.7071 0 0 0.7071"' if rot else ''
        g = f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" {q}/>'
        if mocap is not None and (lvl, i) == mocap:
            # An avatar's hand: no freejoint, position written every tick, still collides.
            p.append(f'<body name="held" mocap="true" pos="{x:g} {y:g} {z:g}">'
                     f'<geom type="box" size="{HX} {HY} {HZ}" {q}/></body>')
        elif owned is None or owned(lvl, i, x, y, z):
            p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>{g}</body>')
        else:
            p.append(f'<geom type="box" size="{HX} {HY} {HZ}" pos="{x:g} {y:g} {z:g}" {q}/>')
    return ''.join(p) + '</worldbody></mujoco>'


def top_of(levels):
    return HZ + (levels - 1) * HZ * 2


def run(xml, steps, pull_speed=0.0, record=False):
    """`pull_speed` in m/s drags the mocap block out along +y."""
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    home = d.mocap_pos.copy() if m.nmocap else None
    for k in range(steps):
        if m.nmocap and pull_speed:
            d.mocap_pos[0] = home[0] + [0.0, pull_speed * k * SUBSTEP, 0.0]
        mujoco.mj_step(m, d)
    pos = d.xpos[1:].copy() if record else None
    return d, m, pos


def standing(d, m, levels):
    zs = d.xpos[1:, 2]
    return float(zs.max()) if len(zs) else 0.0


print(f'MuJoCo {mujoco.__version__} -- tall Jenga, mocap extraction, {SUBSTEP*1000:g} ms substep')

# ── 1. how tall before it falls on its own ──────────────────────────────────────
print('\n1. HOW TALL CAN IT STAND (2 s, nothing touching it)')
for levels in (18, 30, 45, 60):
    t0 = time.perf_counter()
    d, m, _ = run(build(levels), 1000)
    ms = (time.perf_counter() - t0) / 1000 * 1000 * 25
    want, got = top_of(levels), standing(d, m, levels)
    ok = abs(got - want) < HZ * 3
    print(f'   {levels:2d} levels ({levels*PER_LEVEL:3d} blocks, {want*100:5.1f} cm): '
          f'top {got*100:6.2f} cm  {"STANDS" if ok else "FELL":6}  {ms:7.2f} ms/tick')

# ── 2. the extraction, as an avatar motion ──────────────────────────────────────
LEVELS = 30
print(f'\n2. MOCAP EXTRACTION at {LEVELS} levels -- the block is an avatar hand, slid out')
for speed, label in ((0.02, 'slow, 2 cm/s'), (0.10, 'brisk, 10 cm/s'), (0.50, 'yank, 50 cm/s')):
    for lvl, note in ((6, 'low'), (15, 'middle')):
        d, m, _ = run(build(LEVELS, mocap=(lvl, 1)), 1500, pull_speed=speed)
        got = standing(d, m, LEVELS)
        ok = abs(got - top_of(LEVELS)) < HZ * 3
        print(f'   {label:<16} level {lvl:2d} ({note:6}): top {got*100:6.2f} cm  '
              f'{"STANDS" if ok else "COLLAPSES"}')

# ── 3. the same, across zones ───────────────────────────────────────────────────
print(f'\n3. ACROSS ZONES, {LEVELS} levels, brisk pull at level 15')
STEPS = 1200
SPEED = 0.10
MOCAP = (15, 1)
_, _, truth = run(build(LEVELS, mocap=MOCAP), STEPS, pull_speed=SPEED, record=True)


def slabs(n, levels):
    per = levels / n
    return [(lambda lo, hi: (lambda l, i, x, y, z: lo <= l < hi))(int(k*per), int((k+1)*per))
            for k in range(n)]


ids = [(l, i) for (l, i, *_) in blocks(LEVELS) if (l, i) != MOCAP]
for n in (2, 3, 5, 10):
    out = np.full((len(ids), 3), np.nan)
    for owned in slabs(n, LEVELS):
        _, _, pos = run(build(LEVELS, mocap=MOCAP, owned=owned), STEPS,
                        pull_speed=SPEED, record=True)
        k = 0
        for idx, (l, i) in enumerate(ids):
            b = next(bb for bb in blocks(LEVELS) if bb[0] == l and bb[1] == i)
            if owned(l, i, b[2], b[3], b[4]):
                out[idx] = pos[k + 1]      # +1: mocap body occupies row 0 of xpos[1:]
                k += 1
    ok = ~np.isnan(out).any(axis=1)
    drift = np.linalg.norm(out[ok] - truth[1:][ok], axis=1)
    print(f'   {n:2d} zones ({n-1} seams): mean {drift.mean()*1000:7.2f} mm   '
          f'max {drift.max()*1000:8.2f} mm   '
          f'{"agrees" if drift.max() < HZ*2 else "DIVERGED"}')
