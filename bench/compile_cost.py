"""If only things in the physics engine exist, what does creation cost?

MuJoCo compiles its model. A body is not added to a running `mjModel` -- you edit an
`mjSpec` and recompile. So under "only things in the physics engine exist", every act of
creation is a recompile, and a pen drawing continuously recompiles continuously.

That is the question this measures, and the answer decides the architecture:

  - If a recompile of a large world is fast, creation is free and nothing special is
    needed.
  - If it is slow, the usual answer is an object pool, which is a lie about lifetimes
    that leaks into every system that touches it.

Measured two ways, because they are different questions:

  1. `mj_compile` against model size -- what does recompiling the whole world cost.
  2. The same, but splitting the world by MUTATION RATE: a large frozen model that
     recompiles rarely, and a small live model that recompiles constantly. Two models
     stepped together is not pooling -- nothing is reused or pretended dead, the split
     is by how often a thing changes.

Run:  python bench/compile_cost.py

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco

S = 0.02
TICK_MS = 50.0


def model_xml(n_static, n_dynamic=0):
    p = ['<mujoco><option timestep="0.0166"/><worldbody>',
         '<geom name="floor" type="plane" size="60 60 0.1"/>']
    for k in range(n_static):
        x, y, z = (k % 200) * 0.05 - 5, ((k // 200) % 200) * 0.05 - 5, S + (k // 40000) * 0.05
        p.append(f'<geom type="box" size="{S} {S} {S}" pos="{x:g} {y:g} {z:g}"/>')
    for k in range(n_dynamic):
        x, y = (k % 40) * 0.06 - 1.2, (k // 40) * 0.06
        p.append(f'<body pos="{x:g} {y:g} 1.5"><freejoint/>'
                 f'<geom type="box" size="{S} {S} {S}" mass="0.02"/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


def compile_ms(xml, reps=3):
    best = None
    for _ in range(reps):
        t0 = time.perf_counter()
        m = mujoco.MjModel.from_xml_string(xml)
        dt = (time.perf_counter() - t0) * 1000.0
        best = dt if best is None else min(best, dt)
    return best, m


print(f'MuJoCo {mujoco.__version__} -- what does creating a thing cost?')
print('"only things in the physics engine exist" means every creation is a recompile\n')

print('1. RECOMPILING THE WHOLE WORLD')
print(f'   {"geoms in world":>16}{"compile":>12}{"of a 50ms tick":>17}')
for n in (100, 1_000, 5_000, 20_000, 50_000):
    ms, m = compile_ms(model_xml(n))
    flag = '' if ms < TICK_MS else '   <- misses the tick'
    print(f'   {m.ngeom:>16,}{ms:>10.1f} ms{ms/TICK_MS*100:>15.0f}%{flag}')

print('\n2. SPLIT BY MUTATION RATE (not a pool -- nothing is reused or faked)')
print('   a big frozen world that recompiles rarely, beside a small live model that')
print('   recompiles on every stroke:\n')
print(f'   {"frozen world":>14}{"live buffer":>13}{"live recompile":>17}{"of tick":>10}')
for frozen in (5_000, 20_000, 50_000):
    for live in (64, 256):
        ms, m = compile_ms(model_xml(0, live))
        print(f'   {frozen:>14,}{live:>13}{ms:>15.2f} ms{ms/TICK_MS*100:>9.1f}%')
    break_ms, _ = compile_ms(model_xml(frozen))
    print(f'   {"":>14}{"":>13}   frozen side, when a stroke lands: {break_ms:.1f} ms\n')
