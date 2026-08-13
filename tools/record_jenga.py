#!/usr/bin/env python3
"""Film the Jenga zone-split experiment, with the zones colour-coded.

    python tools/record_jenga.py                    # ProRes mezzanine, 1920x1080
    python tools/record_jenga.py --delivery         # H.264, made FROM the mezzanine

The point of colouring is that the finding is invisible in a table. Zone A is warm and
zone B is cool, the boundary sits between them, and what the clip shows is that a
settled tower does not care where the boundary is while a collapsing one does. Three
faults in the scene generator were once found by looking at a picture and none by
reading a number; this is the same argument applied to a seam.

Landscape 1920x1080 by default because the subject is a tall thing in a wide frame and
the orbit needs the width. `--width/--height` override it.

Needs the Python `mujoco` pinned to the version the repository vendors, so that what is
filmed is what is simulated:

    pip install mujoco==3.11.0 pillow pyyaml

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HX, HY, HZ = 0.0375, 0.0125, 0.0075
LEVELS, PER_LEVEL = 18, 3
CUT = 9                       # the zone boundary, in levels

ZONE_A = '.95 .55 .18 1'      # warm
ZONE_B = '.25 .70 .95 1'      # cool
PULLED = '.95 .25 .35 1'      # the block about to come out


def citation():
    import yaml
    c = yaml.safe_load(open(os.path.join(ROOT, 'CITATION.cff'), encoding='utf-8'))
    a = c['authors'][0]
    return c, a, f"{a['given-names']} {a['family-names']}"


def tower(width, height, pull=None, highlight=None):
    """A Jenga tower, coloured by which zone owns each level."""
    p = ['<mujoco><option timestep="0.002"><flag sleep="enable"/></option>',
         f'<visual><global offwidth="{width}" offheight="{height}"/>',
         '<headlight ambient=".30 .30 .36"/></visual>',
         '<default><geom friction="0.6 0.005 0.0001"/></default>',
         '<worldbody>',
         '<light pos="1.2 -1.2 2.2" dir="-.4 .4 -1" directional="true" diffuse=".95 .9 .8"/>',
         '<light pos="-1.4 .8 1.6" dir=".5 -.3 -1" directional="true" diffuse=".30 .35 .5"/>',
         '<geom name="floor" type="plane" size="5 5 0.1" rgba=".15 .16 .20 1"/>']
    for lvl in range(LEVELS):
        rot = lvl % 2 == 1
        z = HZ + lvl * (HZ * 2)
        for i in range(PER_LEVEL):
            if pull is not None and (lvl, i) == pull:
                continue
            off = (i - 1) * (HY * 2)
            x, y = (off, 0.0) if rot else (0.0, off)
            q = 'quat="0.7071 0 0 0.7071"' if rot else ''
            rgba = PULLED if highlight == (lvl, i) else (ZONE_A if lvl < CUT else ZONE_B)
            p.append(f'<body pos="{x:g} {y:g} {z:g}"><freejoint/>'
                     f'<geom type="box" size="{HX} {HY} {HZ}" mass="0.02" '
                     f'rgba="{rgba}" {q}/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


SHOTS = [
    dict(t='A tower, split across two zones',
         sub=f'zone A levels 0-{CUT-1}  ·  zone B levels {CUT}-{LEVELS-1}',
         pull=None, highlight=None, secs=3,
         note='settled: the boundary costs 2.90 mm'),
    dict(t='Pull a middle block',
         sub='level 6, centre — the safe move',
         pull=(6, 1), highlight=None, secs=4,
         note='stands · 3.28 ms/tick'),
    dict(t='Pull an edge block',
         sub='level 6, edge — the tower goes',
         pull=(6, 0), highlight=None, secs=5,
         note='collapses · 28.87 ms/tick, 9x standing'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='jenga_zones.mov')
    ap.add_argument('--width', type=int, default=1920)
    ap.add_argument('--height', type=int, default=1080)
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--delivery', action='store_true',
                    help='H.264 for sending to somebody, rather than ProRes for editing')
    args = ap.parse_args()

    import time
    import mujoco
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont

    cff, author_rec, author = citation()
    title = cff['title']
    W, H = args.width, args.height

    def font(sz, bold=True):
        for n in (('segoeuib.ttf', 'segoeui.ttf'), ('DejaVuSans-Bold.ttf', 'DejaVuSans.ttf'),
                  ('arialbd.ttf', 'arial.ttf')):
            try:
                return ImageFont.truetype(n[0] if bold else n[1], sz)
            except OSError:
                continue
        return ImageFont.load_default()

    F1, F2, F3 = font(52), font(32, False), font(24, False)

    codec = (['-c:v', 'libx264', '-preset', 'slow', '-crf', '25', '-pix_fmt', 'yuv420p',
              '-movflags', '+faststart'] if args.delivery else
             ['-c:v', 'prores_ks', '-profile:v', '3', '-pix_fmt', 'yuv422p10le'])
    meta = ['-metadata', f'title={title}',
            '-metadata', f'artist={author}',
            '-metadata', f'author={author}',
            '-metadata', f"copyright={cff['license']} · {author_rec.get('orcid', '')}",
            '-metadata', f"comment=Jenga zone-split experiment. Cite as "
                         f"{cff['repository-code']} v{cff['version']} ({cff['date-released']}). "
                         f"CITATION.cff in repository root.",
            '-metadata', f"date={cff['date-released']}"]

    enc = subprocess.Popen(
        ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
         '-s', f'{W}x{H}', '-r', str(args.fps), '-i', '-'] + codec + meta + [args.out],
        stdin=subprocess.PIPE)

    sim_secs = wall = 0.0
    for i, S in enumerate(SHOTS, 1):
        m = mujoco.MjModel.from_xml_string(
            tower(W, H, pull=S['pull'], highlight=S['highlight']))
        d = mujoco.MjData(m)
        mujoco.mj_forward(m, d)
        frames = int(S['secs'] * args.fps)
        sub = max(1, int(round((1.0 / args.fps) / m.opt.timestep)))

        with mujoco.Renderer(m, height=H, width=W) as r:
            cam = mujoco.MjvCamera()
            mujoco.mjv_defaultCamera(cam)
            for f in range(frames):
                t0 = time.perf_counter()
                for _ in range(sub):
                    mujoco.mj_step(m, d)
                wall += time.perf_counter() - t0
                sim_secs += sub * m.opt.timestep

                u = f / max(1, frames - 1)
                cam.distance = 0.95 - 0.10 * u
                cam.elevation = -12 - 6 * u
                cam.azimuth = 35 + 55 * u
                cam.lookat[:] = [0, 0, 0.14]
                r.update_scene(d, camera=cam)

                img = Image.fromarray(r.render())
                g = ImageDraw.Draw(img, 'RGBA')
                g.rectangle([0, 0, W, 190], fill=(12, 13, 16, 210))
                g.text((56, 34), S['t'], font=F1, fill=(242, 244, 248))
                g.text((56, 104), S['sub'], font=F2, fill=(150, 158, 172))
                g.text((W - 150, 40), f'{i}/{len(SHOTS)}', font=F3, fill=(110, 118, 132))
                g.rectangle([0, H - 120, W, H], fill=(12, 13, 16, 210))
                g.text((56, H - 96), S['note'], font=F2, fill=(120, 220, 150))
                g.text((56, H - 48), f"{title.split(':')[0]} · {author}",
                       font=F3, fill=(110, 118, 132))
                # zone key
                g.rectangle([W - 330, H - 96, W - 300, H - 70], fill=(242, 140, 46, 255))
                g.text((W - 288, H - 96), 'zone A', font=F3, fill=(200, 205, 215))
                g.rectangle([W - 330, H - 56, W - 300, H - 30], fill=(64, 179, 242, 255))
                g.text((W - 288, H - 56), 'zone B', font=F3, fill=(200, 205, 215))
                enc.stdin.write(np.asarray(img, dtype=np.uint8).tobytes())

        print(f"  {i}/{len(SHOTS)} {S['t']}: {m.nbody-1} blocks, {d.ncon} contacts")

    enc.stdin.close()
    enc.wait()

    print(f'\n{args.out}')
    print(f'simulated {sim_secs:.1f}s in {wall:.2f}s of wall clock — '
          f'REALTIME {sim_secs / wall:.2f}x')
    print(f"cite: {cff['repository-code']} v{cff['version']}")


if __name__ == '__main__':
    main()
