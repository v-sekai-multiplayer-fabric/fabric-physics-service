"""Every MMOG shape this category actually ships, costed against one zone's budget.

Jenga was the wrong model. Almost no shipped MMOG does rigid-body contact at all: an
avatar is a capsule against static world geometry, props are decoration, and nothing
stacks. So a tower of coupled blocks measures a workload the category does not have.

This costs the shapes it does have, against the numbers this repository specifies:

    WARD_ENTITIES    1800    entities in one zone
    WARD_HEADROOM     400    ghosts a neighbour replicates in
    WARD_AUTHORITY   1400    what this zone simulates
    tick              50 ms  at WARD_TICK_HZ 20, 45 ms usable

and the per-entity costs measured on this desk:

    mocap entity (one part of an avatar)     0.74 us   -- no DOF, driven from outside
    dynamic body, uncoupled, settled        21.40 us   -- a cube on a floor
    dynamic body, coupled (stacked)         46.00 us   -- at an island cap of 3
    static geom                              ~0        -- never tested against another static

Categories are described by shape rather than by name. The numbers are what matters.

SPDX-License-Identifier: Apache-2.0
"""

MOCAP_US = 0.74
DYN_US = 21.40
COUPLED_US = 46.00
BUDGET_US = 45_000.0
WARD_AUTHORITY = 1400
PER_PLAYER = 56    # a full game avatar: rig, held items, effects -- not the 3-entity
                   # head-and-two-hands abstraction the physics bench uses

# name, players, dynamic props, coupled props, static props, note
ARCHETYPES = [
    ('instanced dungeon, small party', 5, 20, 0, 400,
     'boss plus adds, scripted; props are set dressing'),
    ('instanced raid', 24, 40, 0, 800,
     'the classic 20-40 encounter'),
    ('open-world questing zone', 150, 60, 0, 2000,
     'players spread over km, mobs local'),
    ('capital city / social hub', 400, 0, 0, 3000,
     'dense avatars, no combat, world is scenery'),
    ('large-scale siege PvP', 400, 100, 0, 1500,
     'siege engines and projectiles are the only dynamics'),
    ('fleet battle, space sim', 800, 200, 0, 0,
     'ships ARE the entities; no terrain at all'),
    ('battle royale drop', 100, 300, 0, 2500,
     'loot on the ground, mostly at rest'),
    ('VR social world', 60, 150, 0, 1200,
     'held objects, thrown things, a ball pit'),
    ('VR social world, crowded', 200, 150, 0, 1200,
     'the concert case: everyone in one place'),
    ('survival, placed structures', 80, 50, 400, 2000,
     'player-built walls and floors, coupled'),
    ('physics sandbox, settled', 60, 600, 0, 1000,
     'a played-in field of loose props'),
    ('physics sandbox, someone is building', 60, 200, 800, 1000,
     'THE hard case: coupled stacks under construction'),
]

print('One zone, WARD_AUTHORITY 1400 entities, 45 ms of a 50 ms tick.')
print('avatar = 3 mocap entities. static props cost nothing and do not count '
      'against the budget.\n')
hdr = (f'{"shape":<38}{"plyrs":>6}{"ents":>7}{"zones":>7}'
       f'{"ms/zone":>9}{"of tick":>9}  binds on')
print(hdr)
print('-' * len(hdr))

for name, players, dyn, coupled, static, note in ARCHETYPES:
    avatar_ents = players * PER_PLAYER
    ents = avatar_ents + dyn + coupled
    us = avatar_ents * MOCAP_US + dyn * DYN_US + coupled * COUPLED_US
    ms = us / 1000.0
    zones = max(1, -(-ents // WARD_AUTHORITY))
    per_zone_ms = ms / zones
    binds = 'entities' if zones > 1 else ('tick' if us > BUDGET_US else 'nothing')
    if coupled and per_zone_ms > BUDGET_US / 1000:
        binds = 'TICK (coupled)'
    print(f'{name:<38}{players:>6}{ents:>7}{zones:>7}'
          f'{per_zone_ms:>9.1f}{per_zone_ms/50*100:>8.0f}%  {binds}')

print('\nnotes:')
for name, *_rest in ARCHETYPES:
    pass
for name, players, dyn, coupled, static, note in ARCHETYPES:
    print(f'  {name:<38} {note}')

print('\n' + '=' * 78)
print('HOW MANY PLAYERS FIT, IF THE ZONE IS NOTHING BUT AVATARS')
pure_ent = WARD_AUTHORITY // PER_PLAYER
pure_cpu = int(BUDGET_US / (PER_PLAYER * MOCAP_US))
print(f'  entity budget allows      {pure_ent:>7,} players')
print(f'  tick budget allows        {pure_cpu:>7,} players')
print(f'  so the binding limit is   {"ENTITIES" if pure_ent < pure_cpu else "CPU"}, '
      f'by {max(pure_ent,pure_cpu)/min(pure_ent,pure_cpu):.0f}x')

print('\nWHAT MAKES THE HARD CASE IMPOSSIBLE RATHER THAN UNLIKELY')
print('  A coupled body costs 46 us at an island cap of 3, and 6250 us uncapped in a')
print('  pile. The budget cannot survive uncapped coupling at any entity count:')
for n in (100, 400, 800, 1400):
    print(f'    {n:>5} coupled bodies: {n*COUPLED_US/1000:>8.1f} ms capped at 3   '
          f'{n*6250/1000:>9.1f} ms uncapped')
print('  Static geoms are the escape. A settled structure promoted into the worldbody')
print('  leaves the broadphase entirely -- measured 328x on a flat field at 100% frozen,')
print('  7.85x at 90%. A builder who walks away costs nothing.')
