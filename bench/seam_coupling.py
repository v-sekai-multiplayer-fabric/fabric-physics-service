"""Is a seam expensive because of motion, or because of coupling?

The Jenga work concluded that a zone boundary costs in proportion to the motion
crossing it. That is not quite right, and the fleet-battle archetype is what exposes it:
eight hundred ships is 186% of one zone's entity budget, so it must be split -- and the
ships are moving fast, which by the motion rule should make the seam expensive.

It does not, because they do not touch each other. A frozen ghost is wrong about where
a body is, but that only matters if something you own depends on where it is. Stacked
blocks depend on their neighbours through contact; ships passing at range depend on
nothing.

So the hypothesis under test: **a seam costs where motion AND coupling cross it.**
Motion alone is free.

Three populations, same body count, same speeds, split the same way:

  1. COUPLED   -- stacked towers, contact chains through the seam
  2. LOOSE     -- bodies on a floor, sparse contact, drifting
  3. FREE      -- bodies in flight, no contact at all (the fleet case)

SPDX-License-Identifier: Apache-2.0
"""
import numpy as np
import mujoco

from human_scale import like

SUBSTEP = 0.002
STEPS = 400
N = 120
HX = HY = HZ = 0.05


def coupled(owned=None):
    """Twelve towers of ten, stacked: contact chains top to bottom."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"/><worldbody>',
         '<geom name="floor" type="plane" size="50 50 0.1"/>']
    k = 0
    for t in range(12):
        cx, cy = (t % 4) * 0.6 - 0.9, (t // 4) * 0.6 - 0.6
        for lvl in range(10):
            z = HZ + lvl * HZ * 2
            p.append(_body(k, cx, cy, z, 0, 0, 0, owned))
            k += 1
    return ''.join(p) + '</worldbody></mujoco>', k


def loose(owned=None):
    """Bodies scattered on a floor with sideways velocity: sparse contact."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"/><worldbody>',
         '<geom name="floor" type="plane" size="50 50 0.1"/>']
    rng = np.random.default_rng(7)
    for k in range(N):
        x, y = rng.uniform(-3, 3), rng.uniform(-3, 3)
        p.append(_body(k, x, y, HZ + 0.02, rng.uniform(-2, 2), rng.uniform(-2, 2), 0, owned))
    return ''.join(p) + '</worldbody></mujoco>', N


def free(owned=None):
    """Bodies in flight, well separated, no floor: nothing ever touches. The fleet."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"><flag gravity="disable"/></option><worldbody>']
    rng = np.random.default_rng(7)
    for k in range(N):
        x, y, z = rng.uniform(-8, 8), rng.uniform(-8, 8), rng.uniform(2, 10)
        p.append(_body(k, x, y, z, rng.uniform(-3, 3), rng.uniform(-3, 3),
                       rng.uniform(-3, 3), owned))
    return ''.join(p) + '</worldbody></mujoco>', N


def _body(k, x, y, z, vx, vy, vz, owned):
    if owned is not None and not owned(k, x, y, z):
        return (f'<geom type="box" size="{HX} {HY} {HZ}" pos="{x:g} {y:g} {z:g}"/>')
    return (f'<body name="b{k}" pos="{x:g} {y:g} {z:g}"><freejoint/>'
            f'<geom type="box" size="{HX} {HY} {HZ}" mass="1"/></body>')


def sim(xml, n_free):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    # Seed velocity from the body's OWN index, never from joint order. Iterating
    # joints gives a split run fewer of them, so the RNG walks out of step and the
    # two runs start from different states -- which reads as seam drift and is not.
    for b in range(1, m.nbody):
        nm = mujoco.mj_id2name(m, mujoco.mjtObj.mjOBJ_BODY, b)
        if not nm or not nm.startswith('b'):
            continue
        j = m.body_jntadr[b]
        if j < 0 or m.jnt_type[j] != mujoco.mjtJoint.mjJNT_FREE:
            continue
        k = int(nm[1:])
        r = np.random.default_rng(1000 + k)
        d.qvel[m.jnt_dofadr[j]:m.jnt_dofadr[j] + 3] = r.uniform(-2, 2, 3)
    for _ in range(STEPS):
        mujoco.mj_step(m, d)
    return d.xpos[1:].copy(), d


def split(builder, halves=2):
    """Zone k owns bodies whose index falls in its slice."""
    xml, n = builder(None)
    truth, dtruth = sim(xml, n)
    out = np.full((n, 3), np.nan)
    per = n / halves
    for h in range(halves):
        lo, hi = int(h * per), int((h + 1) * per)
        own = (lambda lo, hi: (lambda k, x, y, z: lo <= k < hi))(lo, hi)
        x2, _ = builder(own)
        pos, _ = sim(x2, n)
        out[lo:hi] = pos[:hi - lo]
    drift = np.linalg.norm(out - truth, axis=1)
    return drift, dtruth.ncon


print(f'MuJoCo {mujoco.__version__} -- does a seam cost motion, or coupling?')
print(f'{STEPS} steps, {SUBSTEP*1000:g} ms substep, 2 zones, naive frozen ghosts')
print(f'\n{"population":<44}{"contacts":>10}{"mean drift":>13}{"max drift":>12}')
for name, b, note in (
        ('COUPLED  12 towers of 10, stacked', coupled, 'contact chains cross the seam'),
        ('LOOSE    120 bodies sliding on a floor', loose, 'sparse contact'),
        ('FREE     120 bodies in flight, no contact', free, 'the fleet-battle case')):
    drift, ncon = split(b)
    mn, mx = drift.mean()*1000, drift.max()*1000
    print(f'{name:<44}{ncon:>8}{mn:>9.2f} mm{mx:>10.2f} mm   '
          f'max is {like(mx)}')
print('\n  bodies are 100 mm across; the coupled population is the only one that should hurt')
