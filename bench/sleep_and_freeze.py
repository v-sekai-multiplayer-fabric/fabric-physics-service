"""Confirm or deny, with repetitions and across scenes.

Two claims under test:

  A. one_core.md: "sleeping takes the contact count to zero and changes the run time
     not at all."
  B. The rebuttal: sleep is worth 30-1000x, and the original measurement missed it by
     timing across the collapse, when nothing has settled yet.

Both cannot be right. If B holds, the gain must depend on the settle window and must
reproduce across scene shapes. If A holds, the gain vanishes with repetition.

Also under test: freezing settled bodies into the worldbody, which is a different
mechanism -- static geoms are never tested against each other, so those bodies leave
the broadphase rather than merely stopping their solve.
"""
import statistics as st
import time
import mujoco

TIMED = 25          # steps in the timing window
REPS = 3


def _opt(sleep):
    return '<flag sleep="enable"/>' if sleep else ''


def flat(n=900, sleep=False, n_dynamic=None):
    if n_dynamic is None:
        n_dynamic = n
    side = 1
    while side * side < n:
        side += 1
    p = [f'<mujoco><option timestep="0.016666">{_opt(sleep)}</option><worldbody>',
         '<geom name="floor" type="plane" size="200 200 0.1"/>']
    for i in range(n):
        c, r = i % side, i // side
        x, y = c * 0.6 - side * 0.3, r * 0.6 - side * 0.3
        if i < n_dynamic:
            p.append(f'<body pos="{x:g} {y:g} 0.3"><freejoint/>'
                     f'<geom type="box" size="0.2 0.2 0.2" mass="1"/></body>')
        else:
            p.append(f'<geom type="box" size="0.2 0.2 0.2" pos="{x:g} {y:g} 0.3"/>')
    return ''.join(p) + '</worldbody></mujoco>'


def pyramid(layers=8, sleep=False):
    p = [f'<mujoco><option timestep="0.016666">{_opt(sleep)}</option><worldbody>',
         '<geom name="floor" type="plane" size="200 200 0.1"/>']
    for lvl in range(layers):
        w = layers - lvl
        for a in range(w):
            for b in range(w):
                p.append(f'<body pos="{(a-(w-1)*0.5)*0.4:g} {(b-(w-1)*0.5)*0.4:g} '
                         f'{0.2+lvl*0.4:g}"><freejoint/>'
                         f'<geom type="box" size="0.2 0.2 0.2" mass="1"/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


def towers(cols=30, high=20, sleep=False):
    p = [f'<mujoco><option timestep="0.016666">{_opt(sleep)}</option><worldbody>',
         '<geom name="floor" type="plane" size="200 200 0.1"/>']
    side = 1
    while side * side < cols:
        side += 1
    for c in range(cols):
        cx, cy = (c % side) * 3.0 - side * 1.5, (c // side) * 3.0 - side * 1.5
        for k in range(high):
            p.append(f'<body pos="{cx:g} {cy:g} {0.3+k*0.402:g}"><freejoint/>'
                     f'<geom type="box" size="0.2 0.2 0.2" mass="1"/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


def measure(xml, settle):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    for _ in range(settle):
        mujoco.mj_step(m, d)
    t0 = time.perf_counter()
    for _ in range(TIMED):
        mujoco.mj_step(m, d)
    return (time.perf_counter() - t0) / TIMED * 1000.0 * 3, d.ncon


def trial(name, builder, settles):
    print(f'\n=== {name}')
    print(f'{"settle":>7}{"sleep off (ms/tick)":>26}{"sleep on":>22}{"gain":>9}{"ncon off/on":>16}')
    for s in settles:
        off = [measure(builder(sleep=False), s) for _ in range(REPS)]
        on = [measure(builder(sleep=True), s) for _ in range(REPS)]
        mo, mn = st.median([x[0] for x in off]), st.median([x[0] for x in on])
        so = max(x[0] for x in off) - min(x[0] for x in off)
        sn = max(x[0] for x in on) - min(x[0] for x in on)
        print(f'{s:>7}{mo:>15.2f} +/-{so:6.2f}{mn:>13.2f} +/-{sn:6.2f}'
              f'{mo/mn:>8.1f}x{off[0][1]:>9}/{on[0][1]:<6}')


print(f'MuJoCo {mujoco.__version__}, {REPS} reps, {TIMED} timed steps, median +/- spread')
trial('FLAT FIELD, 900 cubes', lambda sleep: flat(900, sleep), [0, 200])
trial('PYRAMID, 204 cubes', lambda sleep: pyramid(8, sleep), [0, 200])
trial('TOWERS, 20 x 12 = 240 cubes', lambda sleep: towers(20, 12, sleep), [0, 200])

print('\n=== FREEZING TO STATIC (flat 900, no sleep flag), settled 150')
base = st.median([measure(flat(900, False), 150)[0] for _ in range(REPS)])
print(f'{"dynamic":>9}{"ms/tick":>20}{"vs all-dynamic":>17}')
for frac in (1.0, 0.2, 0.1):
    n = int(900 * frac)
    r = [measure(flat(900, False, n_dynamic=n), 150)[0] for _ in range(REPS)]
    m = st.median(r)
    print(f'{n:>9}{m:>13.2f} +/-{max(r)-min(r):5.2f}{base/m:>16.2f}x')
