"""Millimetres, in things you can picture.

Drift figures in this bench run from under a millimetre to two metres, and past about
a centimetre the number stops carrying any sense of size. "651 mm of drift" is a
statistic; "the width of a bicycle wheel" is a picture of two zones disagreeing about
where a crate is by that much.

Used by the seam and Jenga benches so a reader who is not holding a ruler can tell
which results matter.

SPDX-License-Identifier: Apache-2.0
"""

# (millimetres, what it is). Ordinary objects, no tools, nothing you would have to
# look up. Kept sorted.
SCALE = [
    (2, 'a grain of rice'),
    (5, 'a pea'),
    (7, 'a pencil is this thick'),
    (10, 'a AAA battery is this thick'),
    (15, 'a Jenga block is this tall'),
    (19, 'a wine cork'),
    (24, 'a coin'),
    (40, 'a golf ball'),
    (67, 'a tennis ball'),
    (85, 'a fist'),
    (95, 'a coffee mug is this tall'),
    (150, 'a bank card is this long'),
    (220, 'a dinner plate'),
    (260, 'a shoe'),
    (300, 'a ruler'),
    (400, 'a laptop is this wide'),
    (450, 'a keyboard'),
    (530, 'a pillow'),
    (660, 'a bicycle wheel'),
    (760, 'a doorway is this wide'),
    (900, 'a kitchen counter is this high'),
    (1200, 'a desk is this wide'),
    (1500, 'a bathtub'),
    (1900, 'a doorway is this tall'),
    (2400, 'a ceiling is this high'),
    (3500, 'a small car is this long'),
]


def like(mm):
    """The nearest everyday object to `mm`, as a phrase."""
    if mm < 1.0:
        return 'under a millimetre'
    best = min(SCALE, key=lambda s: abs(s[0] - mm))
    return best[1]


def fmt(mm, width=9):
    """`'  861.36 mm  (a kitchen counter is this high)'`"""
    return f'{mm:{width}.2f} mm  ({like(mm)})'


if __name__ == '__main__':
    print('every drift figure measured today, in things you can picture:\n')
    for mm, what in ((7.18, 'seam, free bodies, mean'),
                     (14.32, 'Jenga 18-level, collapsing, 1 seam'),
                     (22.76, 'seam, coupled towers, mean'),
                     (534.60, 'seam, coupled towers, max'),
                     (651.27, 'Jenga 30-level, collapsing, 9 seams'),
                     (714.80, 'seam, loose bodies, max'),
                     (861.36, 'seam, free bodies, max'),
                     (1934.01, 'seam, coupled towers, max (first, buggy run)')):
        print(f'  {fmt(mm)}   {what}')
