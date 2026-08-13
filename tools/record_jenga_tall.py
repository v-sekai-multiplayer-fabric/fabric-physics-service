#!/usr/bin/env python3
"""Film a tall Jenga tower across five zones, pulled by an avatar's hand.

    python tools/record_jenga_tall.py               # ProRes mezzanine, 1920x1080
    python tools/record_jenga_tall.py --delivery    # H.264, made FROM the mezzanine

Thirty levels, ninety blocks, forty-four centimetres, cut into five coloured zones. The
block being removed is a MOCAP body -- no degrees of freedom, position written every
tick, still colliding -- which is exactly how `scene.c` drives an avatar's hands. So the
pull is a motion rather than a deletion, and the tower has to survive being handled.

What the clip is for: the numbers say a slow pull at mid-height leaves it standing and
everything else takes it down, and that split across zones a collapsing tower diverges
by 650 mm on a 44 cm tower. Neither reads as anything in a table.

    pip install mujoco==3.11.0 pillow pyyaml

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HX, HY, HZ = 0.0375, 0.0125, 0.0075
LEVELS, PER_LEVEL = 30, 3
ZONES = 5
SUBSTEP = 0.002

# five zones, warm at the base to cool at the top
ZONE_RGBA = ['.95 .50 .15 1', '.95 .72 .22 1', '.55 .82 .40 1',
             '.28 .72 .92 1', '.55 .48 .92 1']
HAND = '.98 .22 .38 1'


def citation():
    import yaml
    c = yaml.safe_load(open(os.path.join(ROOT, 'CITATION.cff'), encoding='utf-8'))
    a = c['authors'][0]
    return c, a, f"{a['given-names']} {a['family-names']}"


def zone_of(lvl):
    return min(ZONES - 1, lvl * ZONES // LEVELS)


def tower(width, height, mocap=None):
    p = [f'<mujoco><option timestep="{SUBSTEP}"><flag sleep="enable"/></option>',
         f'<visual><global offwidth="{width}" offheight="{height}"/>',
         '<headlight ambient=".30 .30 .36"/></visual>',
         '<default><geom friction="0.6 0.005 0.0001"/></default>',
         '<worldbody>',
         '<light pos="1.4 -1.4 2.6" dir="-.4 .4 -1" directional="true" diffuse=".95 .9 .8"/>',
         '<light pos="-1.6 1.0 2.0" dir=".5 -.3 -1" directional="true" diffuse=".30 .35 .5"/>',
         '<geom name="floor" type="plane" size="5 5 0.1" rgba=".14 .15 .19 1"/>']
    for lvl in range(LEVELS):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            off = (i - 1) * (HY * 2)
            x, y = (off, 0.0) if rot else (0.0, off)
            q = 'quat="0.7071 0 0 0.7071"' if rot else ''
            if mocap is not None and (lvl, i) == mocap:
                p.append(f'<body name="held" mocap="true" pos="{x:g} {y:g} {z:g}">'
                         f'<geom type="box" size="{HX} {HY} {HZ}" rgba="{HAND}" {q}/></body>')
            else:
                p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>'
                         f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" '
                         f'rgba="{ZONE_RGBA[zone_of(lvl)]}" {q}/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


SHOTS = [
    dict(t='Thirty levels, five zones', sub='90 blocks · 44 cm · zone per 6 levels',
         mocap=None, speed=0.0, secs=4, note='stands · the seams cost nothing while it is asleep'),
    dict(t='An avatar takes a block, slowly', sub='mocap hand · 2 cm/s · level 15',
         mocap=(15, 1), speed=0.02, secs=6, note='STANDS — the only pull that does'),
    dict(t='The same block, briskly', sub='mocap hand · 10 cm/s · level 15',
         mocap=(15, 1), speed=0.10, secs=6, note='collapses · zones diverge by 650 mm'),
    dict(t='Low and slow is worse than high and fast', sub='mocap hand · 2 cm/s · level 6',
         mocap=(6, 1), speed=0.02, secs=6, note='collapses · height matters more than speed'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='jenga_tall_zones.mov')
    ap.add_argument('--width', type=int, default=1920)
    ap.add_argument('--height', type=int, default=1080)
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--delivery', action='store_true')
    args = ap.parse_args()

    import time
    import mujoco
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont

    cff, author_rec, author = citation()
    title = cff['title']
    W, H = args.width, args.height

    def font(sz, bold=True):
        for a, b in (('segoeuib.ttf', 'segoeui.ttf'), ('DejaVuSans-Bold.ttf', 'DejaVuSans.ttf'),
                     ('arialbd.ttf', 'arial.ttf')):
            try:
                return ImageFont.truetype(a if bold else b, sz)
            except OSError:
                continue
        return ImageFont.load_default()

    F1, F2, F3 = font(52), font(32, False), font(24, False)

    codec = (['-c:v', 'libx264', '-preset', 'slow', '-crf', '25', '-pix_fmt', 'yuv420p',
              '-movflags', '+faststart'] if args.delivery else
             ['-c:v', 'prores_ks', '-profile:v', '3', '-pix_fmt', 'yuv422p10le'])
    meta = ['-metadata', f'title={title}', '-metadata', f'artist={author}',
            '-metadata', f'author={author}',
            '-metadata', f"copyright={cff['license']} · {author_rec.get('orcid', '')}",
            '-metadata', f"comment=Tall Jenga across five zones, avatar-driven extraction. "
                         f"Cite as {cff['repository-code']} v{cff['version']} "
                         f"({cff['date-released']}). CITATION.cff in repository root.",
            '-metadata', f"date={cff['date-released']}"]

    enc = subprocess.Popen(
        ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
         '-s', f'{W}x{H}', '-r', str(args.fps), '-i', '-'] + codec + meta + [args.out],
        stdin=subprocess.PIPE)

    sim_secs = wall = 0.0
    for n, S in enumerate(SHOTS, 1):
        m = mujoco.MjModel.from_xml_string(tower(W, H, mocap=S['mocap']))
        d = mujoco.MjData(m)
        mujoco.mj_forward(m, d)
        home = d.mocap_pos.copy() if m.nmocap else None
        frames = int(S['secs'] * args.fps)
        sub = max(1, int(round((1.0 / args.fps) / SUBSTEP)))
        k = 0

        with mujoco.Renderer(m, height=H, width=W) as r:
            cam = mujoco.MjvCamera()
            mujoco.mjv_defaultCamera(cam)
            for f in range(frames):
                t0 = time.perf_counter()
                for _ in range(sub):
                    if m.nmocap and S['speed']:
                        d.mocap_pos[0] = home[0] + [0.0, S['speed'] * k * SUBSTEP, 0.0]
                    mujoco.mj_step(m, d)
                    k += 1
                wall += time.perf_counter() - t0
                sim_secs += sub * SUBSTEP

                u = f / max(1, frames - 1)
                cam.distance = 1.35 - 0.12 * u
                cam.elevation = -8 - 8 * u
                cam.azimuth = 30 + 60 * u
                cam.lookat[:] = [0, 0, 0.24]
                r.update_scene(d, camera=cam)

                img = Image.fromarray(r.render())
                g = ImageDraw.Draw(img, 'RGBA')
                g.rectangle([0, 0, W, 190], fill=(12, 13, 16, 210))
                g.text((56, 34), S['t'], font=F1, fill=(242, 244, 248))
                g.text((56, 104), S['sub'], font=F2, fill=(150, 158, 172))
                g.text((W - 150, 40), f'{n}/{len(SHOTS)}', font=F3, fill=(110, 118, 132))
                g.rectangle([0, H - 120, W, H], fill=(12, 13, 16, 210))
                g.text((56, H - 96), S['note'], font=F2, fill=(120, 220, 150))
                g.text((56, H - 48), f"{title.split(':')[0]} · {author}",
                       font=F3, fill=(110, 118, 132))
                for zi in range(ZONES):                       # zone key
                    cx = W - 420 + zi * 62
                    rgba = [int(float(v) * 255) for v in ZONE_RGBA[zi].split()[:3]]
                    g.rectangle([cx, H - 92, cx + 44, H - 64], fill=tuple(rgba) + (255,))
                    g.text((cx + 8, H - 58), f'z{zi}', font=F3, fill=(190, 196, 208))
                enc.stdin.write(np.asarray(img, dtype=np.uint8).tobytes())

        print(f"  {n}/{len(SHOTS)} {S['t']}: {m.nbody-1} bodies, {d.ncon} contacts")

    enc.stdin.close()
    enc.wait()
    print(f'\n{args.out}')
    print(f'simulated {sim_secs:.1f}s in {wall:.2f}s of wall clock — '
          f'REALTIME {sim_secs / wall:.2f}x')
    print(f"cite: {cff['repository-code']} v{cff['version']}")


if __name__ == '__main__':
    main()
