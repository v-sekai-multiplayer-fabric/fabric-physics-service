"""The scenario, on a real sketch: 138 strokes drawn with a meshing pen.

`scenario_build.py` used boxes on a grid, which is a fair test of the draw-settle-freeze
loop and a poor model of what a pen leaves behind. This uses the real thing.

`fabric-godot-core/modules/cassie/lean/CassieAvbd/CycleDetect/Fixtures/HatStrokes.lean`
is a recorded CASSIE sketch -- 138 strokes, 234 patches, tessellated at eight samples a
Bezier segment, in metres at human scale. It is the fixture the Lean cycle-detection
proofs run against, so it is the same geometry the pen actually produces.

Each stroke becomes a chain of capsule segments, which is what a stroke IS to a physics
engine: a swept curve with thickness. Then the same three phases as before.

    DRAW      segments dynamic and touching -- the most expensive they ever are
    SETTLE    the sketch comes to rest
    FREEZE    promoted into the worldbody: not simulated, not collided, not knockable

Run:  python bench/scenario_cassie.py

SPDX-License-Identifier: Apache-2.0
"""
import os
import re
import time

import mujoco

FIXTURE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    'fabric-godot-core', 'modules', 'cassie', 'lean', 'CassieAvbd', 'CycleDetect',
    'Fixtures', 'HatStrokes.lean')

SUBSTEP = 1.0 / 60.0
TICK_MS = 50.0
BUDGET_MS = 45.0
RADIUS = 0.004          # pen thickness, metres


def load_strokes(path):
    """Pull the Vec3 arrays straight out of the Lean fixture."""
    txt = open(path, encoding='utf-8').read()
    out = []
    for m in re.finditer(r'private def s\d+ : Array Vec3 := #\[(.*?)\]\n', txt, re.S):
        pts = [tuple(float(v) for v in t.split(','))
               for t in re.findall(r'\(([^()]*)\)', m.group(1))]
        if len(pts) >= 2:
            out.append(pts)
    return out


STROKES = load_strokes(FIXTURE)
SEGMENTS = sum(len(s) - 1 for s in STROKES)


def scene(n_dynamic, sleep=True):
    """First `n_dynamic` segments are bodies; the rest are worldbody geoms."""
    p = [f'<mujoco><option timestep="{SUBSTEP}">',
         '<flag sleep="enable"/>' if sleep else '',
         '</option><default><geom friction="0.7 0.005 0.0001"/></default>',
         '<worldbody><geom name="floor" type="plane" size="20 20 0.1"/>']
    k = 0
    for st in STROKES:
        for a, b in zip(st, st[1:]):
            mid = tuple((a[i] + b[i]) / 2 for i in range(3))
            fx, fy, fz = (a[0] - mid[0], a[1] - mid[1], a[2] - mid[2])
            tx, ty, tz = (b[0] - mid[0], b[1] - mid[1], b[2] - mid[2])
            if max(abs(fx - tx), abs(fy - ty), abs(fz - tz)) < 1e-6:
                k += 1
                continue
            geom = (f'<geom type="capsule" size="{RADIUS}" '
                    f'fromto="{fx:g} {fy:g} {fz:g} {tx:g} {ty:g} {tz:g}"')
            if k < n_dynamic:
                p.append(f'<body pos="{mid[0]:g} {mid[1]:g} {mid[2]:g}"><freejoint/>'
                         f'{geom} mass="0.005"/></body>')
            else:
                p.append(f'<body pos="{mid[0]:g} {mid[1]:g} {mid[2]:g}">{geom}/></body>'
                         .replace('<body', '<geom', 0))
                p[-1] = (f'<geom type="capsule" size="{RADIUS}" '
                         f'fromto="{mid[0]+fx:g} {mid[1]+fy:g} {mid[2]+fz:g} '
                         f'{mid[0]+tx:g} {mid[1]+ty:g} {mid[2]+tz:g}"/>')
            k += 1
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
    return (time.perf_counter() - t0) / timed * 1000.0 * (TICK_MS / (SUBSTEP * 1000)), d, m


def line(label, ms, ncon, extra=''):
    print(f'   {label:<46}{ms:8.2f} ms{ms/TICK_MS*100:7.0f}%{ncon:>8}  '
          f'{"fits" if ms < BUDGET_MS else "OVER":<5}{extra}')


print(f'MuJoCo {mujoco.__version__} -- a real CASSIE sketch through draw/settle/freeze')
print(f'{len(STROKES)} strokes, {SEGMENTS} segments, pen radius {RADIUS*1000:g} mm')
print(f'WARD_AUTHORITY is 1400 entities; this sketch is {SEGMENTS} '
      f'({SEGMENTS/1400*100:.0f}% of one zone)\n')
print(f'   {"phase":<46}{"cost":>11}{"of tick":>7}{"contacts":>8}')

ms, d, m = cost(scene(SEGMENTS), settle=3)
line('1. DRAWING -- every segment dynamic, fresh', ms, d.ncon)

ms, d, m = cost(scene(SEGMENTS), settle=500)
line('2. SETTLED -- still dynamic, asleep', ms, d.ncon)

ms, d, m = cost(scene(0), settle=500)
line('3. FROZEN -- promoted into the worldbody', ms, d.ncon, extra=f'  {m.ngeom} geoms')

print('\n   drawing a fraction of it at a time, the rest already frozen:')
for frac in (0.05, 0.15, 0.30, 1.00):
    n = int(SEGMENTS * frac)
    ms, d, m = cost(scene(n), settle=3)
    line(f'   {n:>5} of {SEGMENTS} segments live ({frac*100:.0f}%)', ms, d.ncon)
