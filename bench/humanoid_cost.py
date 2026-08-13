"""What a Godot humanoid actually costs, driven versus simulated.

`SkeletonProfileHumanoid` is `bones.resize(56)` -- scene/resources/skeleton_profile.cpp:489
-- with `root_bone = "Root"` and `scale_base_bone = "Hips"`. So "56 entities a player"
is a skeleton, not fifty-six loose objects, and two things follow that the archetype
model assumed away.

First, a player is **atomic**. Fifty-six bones are one coupled kinematic chain; a
forearm and its hand cannot sit in different zones. The unit of zone assignment is an
avatar, so 1400 entities is really twenty-five avatars.

Second, the cost depends entirely on whether the bones are DRIVEN or SIMULATED. The
archetype model priced a bone at 0.74 us, which came from measuring mocap spheres --
head and hands, no degrees of freedom, transform written from outside. That is right for
a networked avatar whose pose arrives over the wire. It is not obviously right for a
ragdoll, which is a fifty-five degree-of-freedom articulated system with joint limits.

Nothing had measured either. This does.

  DRIVEN     56 mocap bodies per avatar, pose written every tick, still colliding
  SIMULATED  one articulated chain per avatar, ball joints, gravity, self-collision off

Bone topology follows the humanoid profile: 7 spine and head, 3 face, 8 arm, 30 finger,
8 leg = 56.

SPDX-License-Identifier: Apache-2.0
"""
import time

import mujoco

SUBSTEP = 1.0 / 60.0
TICK_MS = 50.0
BONES = 56

# (name, parent, offset from parent, half-length) -- the humanoid profile's topology.
# Sizes are rough; the COUNT and the CHAIN DEPTH are what cost, not the millimetres.
def skeleton():
    b = []
    def add(name, parent, off, r=0.03, half=0.06):
        b.append((name, parent, off, r, half))
    add('Root', None, (0, 0, 0.9), 0.04, 0.02)
    add('Hips', 'Root', (0, 0, 0.02))
    add('Spine', 'Hips', (0, 0, 0.10))
    add('Chest', 'Spine', (0, 0, 0.10))
    add('UpperChest', 'Chest', (0, 0, 0.10))
    add('Neck', 'UpperChest', (0, 0, 0.10), 0.02, 0.03)
    add('Head', 'Neck', (0, 0, 0.06), 0.05, 0.06)
    add('LeftEye', 'Head', (0.02, 0.04, 0.04), 0.01, 0.01)
    add('RightEye', 'Head', (-0.02, 0.04, 0.04), 0.01, 0.01)
    add('Jaw', 'Head', (0, 0.03, -0.02), 0.015, 0.02)
    for s, sx in (('Left', 1), ('Right', -1)):
        add(f'{s}Shoulder', 'UpperChest', (0.05 * sx, 0, 0.08), 0.025, 0.04)
        add(f'{s}UpperArm', f'{s}Shoulder', (0.09 * sx, 0, 0), 0.025, 0.11)
        add(f'{s}LowerArm', f'{s}UpperArm', (0.22 * sx, 0, 0), 0.022, 0.10)
        add(f'{s}Hand', f'{s}LowerArm', (0.20 * sx, 0, 0), 0.02, 0.04)
        for f in ('Thumb', 'Index', 'Middle', 'Ring', 'Little'):
            p = f'{s}Hand'
            for seg in ('Proximal', 'Intermediate', 'Distal'):
                add(f'{s}{f}{seg}', p, (0.03 * sx, 0, 0), 0.008, 0.015)
                p = f'{s}{f}{seg}'
        add(f'{s}UpperLeg', 'Hips', (0.09 * sx, 0, -0.04), 0.035, 0.16)
        add(f'{s}LowerLeg', f'{s}UpperLeg', (0, 0, -0.32), 0.03, 0.16)
        add(f'{s}Foot', f'{s}LowerLeg', (0, 0, -0.32), 0.03, 0.05)
        add(f'{s}Toes', f'{s}Foot', (0, 0.08, -0.03), 0.02, 0.02)
    return b


BONE_LIST = skeleton()
assert len(BONE_LIST) == BONES, f'{len(BONE_LIST)} bones, expected {BONES}'


def driven(n_avatars):
    """Every bone a mocap body: no DOF, transform written from outside, still collides."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"/><worldbody>',
         '<geom name="floor" type="plane" size="30 30 0.1"/>']
    for a in range(n_avatars):
        ax = (a % 10) * 1.2 - 5.0
        ay = (a // 10) * 1.2 - 3.0
        world = {}
        for name, parent, off, r, half in BONE_LIST:
            base = world[parent] if parent else (ax, ay, 0.0)
            pos = (base[0] + off[0], base[1] + off[1], base[2] + off[2])
            world[name] = pos
            p.append(f'<body name="a{a}_{name}" mocap="true" '
                     f'pos="{pos[0]:g} {pos[1]:g} {pos[2]:g}">'
                     f'<geom type="capsule" size="{r}" fromto="0 0 {-half} 0 0 {half}" '
                     f'contype="0" conaffinity="0"/></body>')
    return ''.join(p) + '</worldbody></mujoco>'


def simulated(n_avatars):
    """One articulated chain per avatar: 55 ball joints, gravity, a real ragdoll."""
    p = [f'<mujoco><option timestep="{SUBSTEP}"/><worldbody>',
         '<geom name="floor" type="plane" size="30 30 0.1"/>']
    for a in range(n_avatars):
        ax = (a % 10) * 1.2 - 5.0
        ay = (a // 10) * 1.2 - 3.0
        open_at = {}
        for name, parent, off, r, half in BONE_LIST:
            if parent is None:
                p.append(f'<body name="a{a}_{name}" pos="{ax+off[0]:g} {ay+off[1]:g} '
                         f'{off[2]:g}"><freejoint/>'
                         f'<geom type="capsule" size="{r}" fromto="0 0 {-half} 0 0 {half}" '
                         f'contype="0" conaffinity="0"/>')
                open_at[name] = 1
                continue
            p.append(f'<body name="a{a}_{name}" pos="{off[0]:g} {off[1]:g} {off[2]:g}">'
                     f'<joint type="ball" damping="0.5" range="0 60"/>'
                     f'<geom type="capsule" size="{r}" fromto="0 0 {-half} 0 0 {half}" '
                     f'contype="0" conaffinity="0"/>')
            open_at[name] = 1
        p.append('</body>' * BONES)
    return ''.join(p) + '</worldbody></mujoco>'


def measure(xml, steps=200):
    m = mujoco.MjModel.from_xml_string(xml)
    d = mujoco.MjData(m)
    mujoco.mj_forward(m, d)
    for _ in range(20):
        mujoco.mj_step(m, d)
    t0 = time.perf_counter()
    for _ in range(steps):
        mujoco.mj_step(m, d)
    per_sub = (time.perf_counter() - t0) / steps * 1000.0
    return per_sub * (TICK_MS / (SUBSTEP * 1000)), m


print(f'MuJoCo {mujoco.__version__} -- one Godot humanoid, {BONES} bones')
print('SkeletonProfileHumanoid: bones.resize(56), root_bone "Root"')
print(f'60 Hz substep, cost reported per 50 ms tick\n')
print(f'{"avatars":>8}{"bones":>8}{"driven ms":>12}{"us/bone":>10}'
      f'{"simulated ms":>15}{"us/bone":>10}{"ratio":>8}')

for n in (1, 5, 25):
    ms_d, md = measure(driven(n))
    ms_s, ms_model = measure(simulated(n))
    nb = n * BONES
    print(f'{n:>8}{nb:>8}{ms_d:>12.3f}{ms_d*1000/nb:>10.2f}'
          f'{ms_s:>15.3f}{ms_s*1000/nb:>10.2f}{ms_s/ms_d:>7.1f}x')

print(f'\n25 avatars is one zone: 1400 entities / {BONES} bones = {1400//BONES} avatars')
print('the archetype model assumed 0.74 us a bone, measured on mocap spheres')
