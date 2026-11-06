#!/usr/bin/env python3
"""Draws the kill feed's own icons (src/obituary.c) and writes src/obit_icons.h.

Original art, drawn here from shapes, in the style of Oni's weapon icons (red fill, orange rim,
alpha 240), so the DLL carries no Oni data. Weapon kills use the weapon class's own icon, read
from the player's game at run time.

Each icon is 64x64 ARGB4444 (Oni pixel type 0, as the stock icons), drawn 4x larger and scaled
down for smooth edges. A part is a shape with a rim: drawn in order, later parts over earlier
ones, each with its own rim, so overlapping parts stay readable (the fingers of the fist).

Usage: gen_obit_icons.py [preview.png]
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 4                    # supersampling
N = 64                   # icon size
RIM = 2.2                # rim width in icon pixels
FILL = (240, 0, 0)
EDGE = (240, 160, 0)
ALPHA = 240


def canvas():
    return Image.new("L", (N * S, N * S), 0)


def erode(mask, px):
    k = int(px * S) | 1
    return mask.filter(ImageFilter.MinFilter(k))


class Icon:
    def __init__(self):
        self.img = Image.new("RGBA", (N * S, N * S), (0, 0, 0, 0))

    def part(self, draw_fn, rim=RIM, cut=None):
        """draw_fn(ImageDraw, scale) paints the shape white on a mask; cut paints holes"""
        m = canvas()
        draw_fn(ImageDraw.Draw(m))
        if cut:
            c = canvas()
            cut(ImageDraw.Draw(c))
            m = ImageChops.subtract(m, c)
        inner = erode(m, rim * 2)
        edge = Image.new("RGBA", m.size, EDGE + (255,))
        fill = Image.new("RGBA", m.size, FILL + (255,))
        layer = Image.new("RGBA", m.size, (0, 0, 0, 0))
        layer.paste(edge, (0, 0), m)
        layer.paste(fill, (0, 0), inner)
        self.img.alpha_composite(layer)

    def done(self):
        im = self.img.resize((N, N), Image.LANCZOS)
        r, g, b, a = im.split()
        a = a.point(lambda v: v * ALPHA // 255)
        return Image.merge("RGBA", (r, g, b, a))


def s(*v):
    return [x * S for x in v]


def rr(d, box, r):
    d.rounded_rectangle(s(*box), radius=r * S, fill=255)


def poly(d, pts):
    d.polygon([(x * S, y * S) for x, y in pts], fill=255)


def star(cx, cy, r_out, r_in, n, rot=0.0, jitter=None):
    pts = []
    for i in range(n * 2):
        r = r_out if i % 2 == 0 else r_in
        if jitter and i % 2 == 0:
            r *= jitter[(i // 2) % len(jitter)]
        a = rot + math.pi * i / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


# ---- Fist (punch and kick: any hand-to-hand hit) ------------------------------------------------
# Seen from the front: the palm block, four fingers curled over it, the thumb across them.

def fist(icon, ox=0.0, oy=0.0, k=1.0):
    def T(x, y):
        return (ox + 32 + (x - 32) * k, oy + 32 + (y - 32) * k)

    def box(x0, y0, x1, y1):
        a, b = T(x0, y0), T(x1, y1)
        return (a[0], a[1], b[0], b[1])

    icon.part(lambda d: rr(d, box(14, 22, 50, 56), 9 * k))                     # palm and wrist side
    for i, (x, top) in enumerate([(13, 14), (22, 11), (31, 11), (40, 13)]):   # fingers
        icon.part(lambda d, x=x, top=top: rr(d, box(x, top, x + 10.5, top + 21), 5 * k))
    icon.part(lambda d: rr(d, box(12, 32, 40, 43), 5.5 * k))                   # thumb


def make_melee():
    icon = Icon()
    fist(icon, oy=2)
    return icon.done()


# ---- Super move: the fist in front of a burst --------------------------------------------------

def make_super():
    icon = Icon()
    icon.part(lambda d: poly(d, star(32, 32, 31, 17, 10, rot=-math.pi / 2,
                                      jitter=[1.0, 0.82, 0.95, 0.8, 1.0, 0.85, 0.93, 0.8, 1.0, 0.86])))
    fist(icon, oy=3, k=0.72)
    return icon.done()


# ---- Explosion (a blast: Mad Bomber's death) ---------------------------------------------------

def make_blast():
    icon = Icon()
    icon.part(lambda d: poly(d, star(32, 33, 31, 15, 11, rot=0.2,
                                      jitter=[1.0, 0.78, 0.92, 0.7, 1.0, 0.84, 0.75, 0.97, 0.8, 0.9, 0.72])))
    icon.part(lambda d: poly(d, star(32, 33, 15, 8, 7, rot=0.6)))
    return icon.done()


# ---- Skull (died some other way) ---------------------------------------------------------------

def make_skull():
    icon = Icon()

    def head(d):
        d.ellipse(s(9, 4, 55, 46), fill=255)
        rr(d, (18, 34, 46, 52), 4)

    def holes(d):
        d.ellipse(s(16, 20, 29, 33), fill=255)        # eyes
        d.ellipse(s(35, 20, 48, 33), fill=255)
        poly(d, [(32, 33), (28.5, 40.5), (35.5, 40.5)])   # nose
        for x in (25.5, 32, 38.5):                    # gaps between the teeth
            d.rectangle(s(x - 1.1, 45, x + 1.1, 53), fill=255)

    icon.part(head, cut=holes)
    rr_jaw = lambda d: rr(d, (21, 54, 43, 60), 3)
    icon.part(rr_jaw)
    return icon.done()


# ---- Fall (a fall killed them) -----------------------------------------------------------------

def make_fall():
    icon = Icon()
    # the ground, broken in two where it was hit, the halves tipped into the dent
    icon.part(lambda d: poly(d, [(2, 47), (29, 52), (27, 61), (2, 61)]))
    icon.part(lambda d: poly(d, [(62, 47), (35, 52), (37, 61), (62, 61)]))
    # the arrow, its point in the break
    icon.part(lambda d: poly(d, [(26, 2), (38, 2), (38, 30), (48, 30), (32, 52), (16, 30), (26, 30)]))
    # debris thrown up either side
    icon.part(lambda d: poly(d, [(6, 36), (13, 33), (15, 38), (9, 41)]))
    icon.part(lambda d: poly(d, [(58, 36), (51, 33), (49, 38), (55, 41)]))
    return icon.done()


ICONS = [("melee", make_melee), ("super", make_super), ("blast", make_blast),
         ("skull", make_skull), ("fall", make_fall)]


def argb4444(im):
    out = []
    for r, g, b, a in im.getdata():
        out.append((a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4))
    return out


def main():
    root = Path(__file__).resolve().parent.parent
    images = [(name, fn()) for name, fn in ICONS]
    lines = [
        "/* Generated by tools/gen_obit_icons.py: the kill feed's own icons (original art, no Oni data),",
        " * 64x64 ARGB4444, row by row from the top. Don't edit; run the script. */",
        "#define OBIT_ICON_SIZE 64",
        "enum { " + ", ".join("OBIT_ICON_%s" % n.upper() for n, _ in images) + ", OBIT_ICONS };",
        "static const unsigned short g_obit_icon_texels[OBIT_ICONS][OBIT_ICON_SIZE * OBIT_ICON_SIZE] = {",
    ]
    for name, im in images:
        px = argb4444(im)
        lines.append("\t{ /* %s */" % name)
        for i in range(0, len(px), 16):
            lines.append("\t\t" + ", ".join("0x%04x" % v for v in px[i:i + 16]) + ",")
        lines.append("\t},")
    lines.append("};")
    (root / "src" / "obit_icons.h").write_text("\n".join(lines) + "\n")

    if len(sys.argv) > 1:
        scale = 2
        sheet = Image.new("RGBA", (len(images) * (N * scale + 8), N * scale), (40, 40, 40, 255))
        for i, (_, im) in enumerate(images):
            sheet.alpha_composite(im.resize((N * scale, N * scale), Image.NEAREST), (i * (N * scale + 8), 0))
        sheet.save(sys.argv[1])
    print("wrote src/obit_icons.h (%d icons)" % len(images))


if __name__ == "__main__":
    main()
