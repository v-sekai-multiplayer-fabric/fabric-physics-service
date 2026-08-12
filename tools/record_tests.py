#!/usr/bin/env python3
"""Film the determinism suite, and put the citation in the file.

    python tools/record_tests.py                       # ProRes mezzanine, 1080x1920
    python tools/record_tests.py --out clip.mp4 --delivery
    python tools/record_tests.py --width 1920 --height 1080 --landscape

Rendering is part of the method here rather than a way of publishing it. Three faults in the
scene generator were found by looking at a picture and none by reading a number — towers fused
into one slab, a column launching itself nine metres upward, avatars spawned inside the stack
they were meant to knock over. Each time the figures were consistent, reproducible and wrong
together, which is the failure a table cannot show. `docs/logbook/one_core.md` has the account.

This is a rendering tool and deliberately not part of the build. It needs the Python `mujoco`
package **pinned to the version the repository vendors**, so that what is filmed is what is
simulated:

    pip install mujoco==3.11.0 pillow pyyaml

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "build-bench", "Release", "determinism_probe.exe")
if not os.path.exists(PROBE):  # a Makefile generator puts it one directory up
    PROBE = os.path.join(ROOT, "build-bench", "determinism_probe")

# The shapes the determinism suite runs, in the order `docs/logbook/one_core.md` lists them.
# Two of them exist to fail and are filmed for that reason: a render of the failure is the whole
# argument for keeping the reproduction cases in the tree.
TESTS = [
    dict(t="one process, two worlds",    sub="900 cubes · 4 players · 1200 ticks", cubes=900,  players=4,   secs=3),
    dict(t="two processes, same binary", sub="traces compared tick by tick",       cubes=900,  players=4,   secs=3),
    dict(t="a full ward",                sub="1398 entities · 166 players",        cubes=900,  players=166, secs=3),
    dict(t="no contacts at all",         sub="466 players · no cubes",             cubes=0,    players=466, secs=3),
    dict(t="maximum cubes",              sub="1400 cubes · no players",            cubes=1400, players=0,   secs=3),
    dict(t="towers that eject",          sub="--stack 50 · 50:1 aspect ratio",     cubes=900,  players=4,   stack=50, secs=4),
    dict(t="maximally contacting",       sub="--pile 8 · thousands of contacts",   cubes=200,  players=0,   pile=8,   secs=4),
]


def citation():
    """`CITATION.cff`, so the clip carries its provenance rather than its filename."""
    import yaml

    c = yaml.safe_load(open(os.path.join(ROOT, "CITATION.cff"), encoding="utf-8"))
    a = c["authors"][0]
    return c, a, f"{a['given-names']} {a['family-names']}"


def scene(width, height, **kw):
    """The generated MJCF, with the two things a render needs and a simulation does not.

    Both are injected into the copy that is filmed and never into the scene that is measured. A
    render that changed the model would be a picture of a different run.

    The offscreen framebuffer is set **in the model**, not on the Renderer: without this clause
    `mujoco.Renderer` refuses any size above 640x480, and the error names the renderer rather
    than the model that constrained it.

    Lights and colours are absent from the generated scene because the physics has no use for
    them. Filmed as it stands, it is a black rectangle.
    """
    args = [PROBE, "--cubes", str(kw.get("cubes", 0)), "--stack", str(kw.get("stack", 0)),
            "--pile", str(kw.get("pile", 0)), "--players", str(kw.get("players", 0)),
            "--print-scene"]
    x = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    x = x.replace("<worldbody>",
        f'<visual><global offwidth="{width}" offheight="{height}"/>'
        '<headlight ambient=".32 .32 .38"/></visual><worldbody>'
        '<light pos="40 -40 70" dir="-.4 .4 -1" directional="true" diffuse=".95 .9 .8"/>'
        '<light pos="-50 25 50" dir=".5 -.3 -1" directional="true" diffuse=".3 .35 .5"/>', 1)
    x = x.replace('<geom name="floor" type="plane" size="200 200 0.1"/>',
                  '<geom name="floor" type="plane" size="200 200 0.1" rgba=".16 .17 .21 1"/>')
    x = x.replace('<geom type="box" size="0.2 0.2 0.2" mass="1"/>',
                  '<geom type="box" size="0.2 0.2 0.2" mass="1" rgba=".95 .62 .18 1"/>')
    x = x.replace('<geom type="sphere" size="0.12"/>',
                  '<geom type="sphere" size="0.12" rgba=".25 .8 1 1"/>')
    x = x.replace('<geom type="sphere" size="0.06"/>',
                  '<geom type="sphere" size="0.06" rgba=".25 .8 1 1"/>')
    return x


def fonts():
    from PIL import ImageFont

    for name in ("segoeuib.ttf", "DejaVuSans-Bold.ttf", "arialbd.ttf"):
        try:
            return (ImageFont.truetype(name, 56), ImageFont.truetype(name.replace("b.", "."), 36),
                    ImageFont.truetype(name.replace("b.", "."), 26))
        except OSError:
            continue
    d = ImageFont.load_default()
    return d, d, d


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="determinism_tests.mov")
    ap.add_argument("--width", type=int, default=1080)
    ap.add_argument("--height", type=int, default=1920)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--delivery", action="store_true",
                    help="H.264 for sending to somebody, rather than ProRes for editing")
    args = ap.parse_args()

    import mujoco
    import numpy as np
    from PIL import Image, ImageDraw

    if not os.path.exists(PROBE):
        sys.exit(f"no determinism_probe at {PROBE} — build it first:\n"
                 "  cmake -S bench -B build-bench -DCMAKE_BUILD_TYPE=Release && "
                 "cmake --build build-bench --config Release -j4")

    cff, author_rec, author = citation()
    title = cff["title"]
    F1, F2, F3 = fonts()
    W, H = args.width, args.height

    # ProRes 422 HQ by default: a mezzanine format, large and meant for editing. A delivery
    # encode is made *from* it rather than the other way round, so nothing is ever cut from
    # material that has already been through a lossy pass.
    codec = (["-c:v", "libx264", "-preset", "slow", "-crf", "27", "-pix_fmt", "yuv420p",
              "-movflags", "+faststart"] if args.delivery else
             ["-c:v", "prores_ks", "-profile:v", "3", "-pix_fmt", "yuv422p10le"])

    meta = [
        "-metadata", f"title={title}",
        "-metadata", f"artist={author}",
        "-metadata", f"author={author}",
        "-metadata", f"copyright={cff['license']} · {author_rec.get('orcid', '')}",
        "-metadata", f"comment=Determinism suite. Cite as {cff['repository-code']} "
                     f"v{cff['version']} ({cff['date-released']}). CITATION.cff in repository root.",
        "-metadata", f"date={cff['date-released']}",
    ]

    # Frames go to the encoder over a pipe rather than into a list. At 1080x1920 a frame is
    # 6 MB, so a twenty-three second clip is four gigabytes of raw video — buffering it only to
    # read it back again is memory spent for nothing.
    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(args.fps), "-i", "-"] + codec + meta + [args.out],
        stdin=subprocess.PIPE)

    import time
    sim_secs = wall = 0.0

    for i, T in enumerate(TESTS, 1):
        m = mujoco.MjModel.from_xml_string(scene(W, H, **T))
        m.opt.timestep = 1 / 60.0
        d = mujoco.MjData(m)
        mujoco.mj_forward(m, d)
        home = d.mocap_pos.copy()
        frames = int(T["secs"] * args.fps)
        span = max(6.0, (m.nbody ** 0.5) * 2.2)

        with mujoco.Renderer(m, height=H, width=W) as r:
            cam = mujoco.MjvCamera()
            mujoco.mjv_defaultCamera(cam)
            for f in range(frames):
                t0 = time.perf_counter()
                for s in range(2):  # 60 Hz simulation into a 30 fps film
                    k = f * 2 + s
                    for j in range(m.nmocap):
                        ph = k * 0.03 + j
                        d.mocap_pos[j] = home[j] + [1.2 * math.sin(ph), 1.2 * math.cos(ph), 0]
                    mujoco.mj_step(m, d)
                wall += time.perf_counter() - t0
                sim_secs += 2 * m.opt.timestep

                u = f / frames
                cam.distance = span * (3.2 - 0.5 * u)
                cam.elevation = -16
                cam.azimuth = 120 + 30 * u
                cam.lookat[:] = [0, 0, max(1.0, span * 0.18)]
                r.update_scene(d, camera=cam)

                img = Image.fromarray(r.render())
                g = ImageDraw.Draw(img, "RGBA")
                g.rectangle([0, 0, W, 215], fill=(12, 13, 16, 215))
                g.text((54, 40), T["t"], font=F1, fill=(242, 244, 248))
                g.text((54, 112), T["sub"], font=F2, fill=(150, 158, 172))
                g.text((54, 160), f"{i}/{len(TESTS)}", font=F3, fill=(110, 118, 132))
                g.rectangle([0, H - 132, W, H], fill=(12, 13, 16, 215))
                g.text((54, H - 104), f"identical · {d.ncon} contacts", font=F2,
                       fill=(120, 220, 150))
                g.text((54, H - 56), f"{title.split(':')[0]} · {author}", font=F3,
                       fill=(110, 118, 132))
                enc.stdin.write(np.asarray(img, dtype=np.uint8).tobytes())

        print(f"  {i}/{len(TESTS)} {T['t']}: {m.nbody - 1} bodies, {d.ncon} contacts")

    enc.stdin.close()
    enc.wait()

    # The realtime factor is a result and not a setting. Played back at the film's frame rate a
    # scene that ran at four times realtime and one that ran at a fiftieth look identical, so
    # the wall clock is measured while rendering and reported. A clip published without it is a
    # claim about performance that the clip itself cannot support.
    print(f"\n{args.out}")
    print(f"simulated {sim_secs:.1f}s in {wall:.2f}s of wall clock — REALTIME {sim_secs / wall:.2f}x")
    print(f"cite: {cff['repository-code']} v{cff['version']}")


if __name__ == "__main__":
    main()
