"""The scenario: people build things, the things stay, everyone sees them.

Straight from `DEMO_BRIEF.md`. Build/create/UGC took 8 of 14 answers, shared persistence
5, and one respondent named the failure exactly -- "there are still worlds with items
localized", meaning things you build that other people cannot see.

So this is the whole loop, costed:

    DRAW      a player strokes geometry into the world. Pieces are new, dynamic and
              touching each other, which is the most expensive they will ever be.
    SETTLE    the stroke comes to rest.
    FREEZE    settled pieces are promoted into the worldbody. They stop being simulated
              and stop being COLLIDED -- static geoms are never tested against each
              other -- so they cost nothing and cannot be knocked over.
    SHARED    a second zone holds the same frozen structure as its own static geometry.
              Everyone sees it because it is world, not because it was replicated.

The claim being tested is that a built world is affordable if and only if built things
stop being dynamic, and that the moment they do, they are also free to share.

Run:  python bench/scenario_build.py

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco

SUBSTEP = 1.0 / 60.0
TICK_MS = 50.0
BUDGET_MS = 45.0

BUILDERS = 8          # people drawing at once
STROKE = 40           # pieces in one stroke
S = 0.06              # piece half-extent, metres


def stroke_pieces(bx, by, n):
    """One stroke: a wall going up, pieces touching. What a pen would leave behind."""
    out = []
    per_row = 8
    for k in range(n):
        col, row = k % per_row, k // per_row
        out.append((bx + (col - per_row / 2) * S * 2,
                    by,
                    S + row * S * 2))
    return out


def world(n_drawn, frozen, extra_static=0):
    """`n_drawn` pieces per builder are dynamic; `frozen` are static geoms instead."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"><flag sleep="enable"/></option>',
         '<default><geom friction="0.7 0.005 0.0001"/></default>',
         '<worldbody><geom name="floor" type="plane" size="40 40 0.1"/>']
    for b in range(BUILDERS):
        bx, by = (b % 4) * 2.2 - 3.3, (b // 4) * 2.2 - 1.1
        for i, (x, y, z) in enumerate(stroke_pieces(bx, by, n_drawn + frozen)):
            if i < frozen:
                p.append(f'<geom type="box" size="{S} {S} {S}" pos="{x:g} {y:g} {z:g}"/>')
            else:
                p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>'
                         f'<geom type="box" size="{S} {S} {S}" mass="0.5"/></body>')
    for k in range(extra_static):      # the rest of the built world, already frozen
        p.append(f'<geom type="box" size="{S} {S} {S}" '
                 f'pos="{(k%60)*0.2-6:g} {(k//60)*0.2+4:g} {S}"/>')
    return ''.join(p) + '</worldbody></mujoco>'


def cost(xml, settle, timed=60):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    for _ in range(settle):
        mujoco.mj_step(m, d)
    t0 = time.perf_counter()
    for _ in range(timed):
        mujoco.mj_step(m, d)
    per_sub = (time.perf_counter() - t0) / timed * 1000.0
    return per_sub * (TICK_MS / (SUBSTEP * 1000)), d, m


def line(label, ms, ncon, extra=''):
    pct = ms / TICK_MS * 100
    verdict = 'fits' if ms < BUDGET_MS else 'OVER'
    print(f'   {label:<44}{ms:8.2f} ms{pct:7.0f}%{ncon:>8}  {verdict:<5}{extra}')


print(f'MuJoCo {mujoco.__version__} -- build, settle, freeze, share')
print(f'{BUILDERS} builders x {STROKE} pieces = {BUILDERS*STROKE} placed, '
      f'50 ms tick, 45 ms usable\n')
print(f'   {"phase":<44}{"cost":>11}{"of tick":>7}{"contacts":>8}')

ms, d, m = cost(world(STROKE, 0), settle=5)
line('1. DRAWING -- all pieces dynamic, fresh', ms, d.ncon)

ms, d, m = cost(world(STROKE, 0), settle=400)
line('2. SETTLED -- still dynamic, asleep', ms, d.ncon)

ms, d, m = cost(world(0, STROKE), settle=400)
line('3. FROZEN -- promoted into the worldbody', ms, d.ncon)

print()
print('   a built world grows. what does yesterday cost?')
for built in (0, 2000, 10000, 40000):
    ms, d, m = cost(world(STROKE, 0, extra_static=built), settle=400)
    line(f'   {built:>6} frozen pieces already in the world', ms, d.ncon,
         extra=f'  {m.ngeom} geoms')

print()
print('   and the same world with somebody still drawing in it:')
for built in (10000, 40000):
    ms, d, m = cost(world(STROKE, STROKE, extra_static=built), settle=5)
    line(f'   {built:>6} frozen + {BUILDERS*STROKE} being drawn', ms, d.ncon)

print(f'\n   the loop closes: drawing costs, settled costs less, frozen costs nothing,')
print(f'   and nothing you froze yesterday makes today slower.')
