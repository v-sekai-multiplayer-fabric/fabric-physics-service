"""A zone at its actual entity budget, built out of Jenga towers.

Every Jenga measurement before this used a toy: fifty-four blocks, or ninety. This uses
the numbers the design actually specifies.

    WARD_ENTITIES    1800   entities in one zone
    WARD_HEADROOM     400   of those, ghosts a neighbour replicates in
    WARD_AUTHORITY   1400   the rest -- what this zone simulates

So a zone is 1400 own bodies plus 400 static ghosts, and the question is whether that
fits a 50 ms tick when the contents are the hardest thing a sandbox produces: towers
that stand only on friction.

1400 does not fit in one tower -- 466 levels is seven metres and nothing stands at that
aspect ratio. So a zone is a FIELD of towers, which is also what a played-in zone looks
like. Fifteen towers of thirty levels is 1350, plus fifty loose blocks, is 1400 exactly.

Looking for what is legal rather than what is fast: which configurations fit the budget,
and which do not.

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco

HX, HY, HZ = 0.0375, 0.0125, 0.0075
PER_LEVEL = 3
SUBSTEP = 0.002
TICK_MS = 50.0

WARD_ENTITIES = 1800
WARD_HEADROOM = 400
WARD_AUTHORITY = WARD_ENTITIES - WARD_HEADROOM      # 1400

TOWER_LEVELS = 30                                   # the tallest that stands
PER_TOWER = TOWER_LEVELS * PER_LEVEL                # 90
SPACING = 0.45                                      # metres between towers


def tower_at(p, cx, cy, levels, rgba_owned, dynamic, skip=None):
    n = 0
    for lvl in range(levels):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            if skip is not None and (lvl, i) == skip:
                continue
            off = (i - 1) * (HY * 2)
            x, y = (cx + off, cy) if rot else (cx, cy + off)
            q = 'quat="0.7071 0 0 0.7071"' if rot else ''
            if dynamic:
                p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>'
                         f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" {q}/></body>')
            else:
                p.append(f'<geom type="box" size="{HX} {HY} {HZ}" '
                         f'pos="{x:g} {y:g} {z:g}" {q}/>')
            n += 1
    return n


def zone(own, ghosts, sleep=True, topple=False):
    """`own` dynamic entities in towers, `ghosts` static entities beside them."""
    p = [f'<mujoco><option timestep="{SUBSTEP}">',
         '<flag sleep="enable"/>' if sleep else '',
         '</option><default><geom friction="0.6 0.005 0.0001"/></default>',
         '<worldbody><geom name="floor" type="plane" size="20 20 0.1"/>']
    placed = 0
    col = 0
    while placed < own:
        cx = (col % 5) * SPACING - 2 * SPACING
        cy = (col // 5) * SPACING - 2 * SPACING
        left = own - placed
        levels = min(TOWER_LEVELS, max(1, left // PER_LEVEL))
        skip = (6, 0) if (topple and col == 0) else None
        placed += tower_at(p, cx, cy, levels, None, True, skip=skip)
        col += 1
        if col > 200:
            break
    # ghosts: a neighbour's towers, replicated in as static geoms
    g = 0
    col = 0
    while g < ghosts:
        cx = (col % 5) * SPACING - 2 * SPACING + 5 * SPACING
        cy = (col // 5) * SPACING - 2 * SPACING
        levels = min(TOWER_LEVELS, max(1, (ghosts - g) // PER_LEVEL))
        g += tower_at(p, cx, cy, levels, None, False)
        col += 1
        if col > 200:
            break
    return ''.join(p) + '</worldbody></mujoco>', placed, g


def run(xml, settle, timed=60):
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


print(f'MuJoCo {mujoco.__version__} -- one zone at the specified budget')
print(f'WARD_ENTITIES {WARD_ENTITIES} = WARD_AUTHORITY {WARD_AUTHORITY} own '
      f'+ WARD_HEADROOM {WARD_HEADROOM} ghosts')
print(f'towers of {TOWER_LEVELS} levels ({PER_TOWER} blocks), 50 ms tick, '
      f'{SUBSTEP*1000:g} ms substep\n')

print('1. DOES A ZONE FIT ITS OWN BUDGET')
print(f'{"configuration":<44}{"settled":>12}{"of tick":>10}{"contacts":>10}')
for own, ghosts, label in (
        (WARD_AUTHORITY, 0, f'{WARD_AUTHORITY} own, no ghosts'),
        (WARD_AUTHORITY, WARD_HEADROOM, f'{WARD_AUTHORITY} own + {WARD_HEADROOM} ghosts'),
        (WARD_ENTITIES, 0, f'{WARD_ENTITIES} own (ghosts simulated too)'),
        (700, WARD_HEADROOM, f'700 own + {WARD_HEADROOM} ghosts (half a zone)')):
    xml, n_own, n_gh = zone(own, ghosts)
    ms, d, m = run(xml, settle=400)
    print(f'   {label:<41}{ms:9.2f} ms{ms/TICK_MS*100:9.0f}%{d.ncon:10d}')

print('\n2. WHAT A COLLAPSE COSTS AT BUDGET')
xml, _, _ = zone(WARD_AUTHORITY, WARD_HEADROOM, topple=True)
ms, d, m = run(xml, settle=40)
print(f'   {WARD_AUTHORITY} own + {WARD_HEADROOM} ghosts, one tower toppling: '
      f'{ms:.2f} ms/tick ({ms/TICK_MS*100:.0f}% of tick), {d.ncon} contacts')

print('\n3. SLEEP ON AND OFF AT BUDGET')
for sleep in (True, False):
    xml, _, _ = zone(WARD_AUTHORITY, WARD_HEADROOM, sleep=sleep)
    ms, d, m = run(xml, settle=400)
    print(f'   sleep {"on " if sleep else "off"}: {ms:8.2f} ms/tick  '
          f'({ms/TICK_MS*100:5.0f}% of tick)  contacts {d.ncon}')

print('\n4. GHOSTS: WHAT DOES THE HEADROOM COST')
for ghosts in (0, 200, 400, 800):
    xml, _, g = zone(WARD_AUTHORITY, ghosts)
    ms, d, m = run(xml, settle=400)
    print(f'   {g:4d} static ghosts beside {WARD_AUTHORITY} own: {ms:8.2f} ms/tick')
