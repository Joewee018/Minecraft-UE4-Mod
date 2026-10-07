"""Craft 64 art generator (original pixel art in Minecraft's texture style; no Doom or Mojang image data).

Writes into Bridge/src/main/resources/assets/crossover_rebuilt:
  textures/c64/<weapon>_0/1/2.png   first-person weapon sprites (Doom-style, held by Steve's blocky arms), 128x96
  textures/item/c64_<weapon>.png     16x16 pickup items
  models/item/c64_<weapon>.json, lang/en_us.json
Run: python Tools/c64_art.py [--preview out.png]
"""
import json, os, sys, math, random
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'Bridge', 'src', 'main', 'resources', 'assets', 'crossover_rebuilt')
W, H = 128, 96

# ---------------------------------------------------------------- Minecraft-style materials (3-5 shade ramps)
MAT = {
    'iron':     [(110, 110, 110), (150, 150, 150), (190, 190, 190), (216, 216, 216), (240, 240, 240)],
    'steel':    [(52, 52, 58), (72, 72, 80), (92, 92, 100), (112, 112, 122), (135, 135, 145)],
    'obsidian': [(16, 12, 26), (26, 20, 40), (40, 28, 62), (58, 40, 90), (80, 60, 120)],
    'oak':      [(104, 80, 44), (126, 98, 55), (150, 118, 70), (170, 136, 84), (190, 154, 98)],
    'darkoak':  [(40, 26, 12), (54, 36, 17), (66, 43, 20), (80, 54, 28), (96, 66, 36)],
    'skin':     [(106, 70, 52), (138, 94, 72), (158, 112, 88), (176, 128, 102), (192, 146, 118)],   # Steve's tan
    'gold':     [(170, 110, 20), (210, 150, 30), (240, 190, 50), (252, 220, 80), (255, 245, 150)],
    'redstone': [(90, 0, 0), (130, 0, 0), (175, 10, 10), (220, 20, 20), (255, 70, 60)],
    'diamond':  [(20, 100, 100), (40, 150, 150), (80, 200, 195), (120, 230, 225), (190, 250, 245)],
    'emerald':  [(10, 90, 40), (20, 130, 60), (40, 180, 85), (70, 220, 115), (150, 250, 170)],
    'nether':   [(70, 18, 18), (95, 28, 28), (115, 38, 38), (135, 50, 48), (160, 70, 66)],
    'bone':     [(170, 165, 140), (200, 195, 170), (220, 215, 190), (235, 230, 210), (250, 248, 235)],
    'stone':    [(90, 90, 90), (110, 110, 110), (125, 125, 125), (140, 140, 140), (160, 160, 160)],
    'copper':   [(110, 50, 30), (150, 70, 45), (190, 100, 70), (215, 125, 90), (235, 160, 120)],
    'fire':     [(200, 60, 10), (240, 120, 20), (255, 180, 40), (255, 225, 90), (255, 250, 200)],
    'soul':     [(20, 90, 140), (40, 150, 200), (90, 210, 240), (150, 240, 255), (230, 255, 255)],
    'bfglow':   [(20, 120, 30), (60, 200, 60), (130, 255, 110), (200, 255, 170), (245, 255, 235)],
    'laser':    [(120, 0, 0), (200, 20, 10), (255, 60, 40), (255, 150, 120), (255, 230, 220)],
    'sleeve':   [(0, 110, 110), (0, 140, 140), (0, 165, 165), (20, 185, 185), (60, 205, 205)],
}

def noise(x, y, seed):
    h = (x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)
    h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
    return (h & 0xffff) / 65535.0


class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.px = [[None] * w for _ in range(h)]       # (rgb, layer-id)
        self.layer = 0

    def fill(self, mask_fn, mat, light=0.5, seed=1, grain=2, outline=True):
        """mask_fn(x, y) -> bool. light: 0..1 base brightness; vertical gradient + Minecraft-style pixel noise."""
        self.layer += 1
        ramp = MAT[mat]; cells = []
        for y in range(self.h):
            for x in range(self.w):
                if mask_fn(x, y): cells.append((x, y))
        cs = set(cells)
        for x, y in cells:
            n = noise(x // grain, y // grain, seed)
            v = light + (n - 0.5) * 0.55
            edge = outline and any((x + dx, y + dy) not in cs for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            top_lit = (x, y - 1) not in cs or (x - 1, y) not in cs
            if edge: v = v - 0.45 if not top_lit else v + 0.25
            i = max(0, min(len(ramp) - 1, int(round(v * (len(ramp) - 1)))))
            self.px[y][x] = ramp[i]

    def rect(self, x0, y0, x1, y1, mat, light=0.5, seed=1, **k):
        self.fill(lambda x, y: x0 <= x < x1 and y0 <= y < y1, mat, light, seed, **k)

    def poly(self, pts, mat, light=0.5, seed=1, **k):
        def inside(x, y):
            px, py = x + 0.5, y + 0.5; c = False; n = len(pts)
            for i in range(n):
                xa, ya = pts[i]; xb, yb = pts[(i + 1) % n]
                if (ya > py) != (yb > py) and px < (xb - xa) * (py - ya) / (yb - ya) + xa: c = not c
            return c
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        bx0, bx1, by0, by1 = int(min(xs)), int(max(xs)) + 1, int(min(ys)), int(max(ys)) + 1
        self.fill(lambda x, y: bx0 <= x < bx1 and by0 <= y < by1 and inside(x, y), mat, light, seed, **k)

    def ellipse(self, cx, cy, rx, ry, mat, light=0.5, seed=1, **k):
        self.fill(lambda x, y: ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1, mat, light, seed, **k)

    def flash(self, cx, cy, r, mat='fire', seed=7, spikes=8):
        """Blocky muzzle flash: a star of fire pixels (hot centre)."""
        def m(x, y):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            a = math.atan2(dy, dx); d = math.hypot(dx, dy)
            lim = r * (0.55 + 0.45 * abs(math.cos(a * spikes / 2))) * (0.85 + 0.3 * noise(int(a * 10), 0, seed))
            return d <= lim
        self.layer += 1
        for y in range(self.h):
            for x in range(self.w):
                if m(x, y):
                    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy) / max(1, r)
                    ramp = MAT[mat]; i = max(0, min(4, int(round((1 - d) * 4 + (noise(x // 2, y // 2, seed) - 0.5)))))
                    self.px[y][x] = ramp[i]

    def image(self, scale=1):
        im = Image.new('RGBA', (self.w, self.h), (0, 0, 0, 0))
        for y in range(self.h):
            for x in range(self.w):
                c = self.px[y][x]
                if c: im.putpixel((x, y), c + (255,))
        return im.resize((self.w * scale, self.h * scale), Image.NEAREST) if scale != 1 else im


# ---------------------------------------------------------------- Steve's blocky arms (Minecraft first-person look)
def limb(c, hx, hy, w, bx, side=1, grip=True):
    """Steve's square arm: a hand block at (hx, hy) (top-left, w wide) and the forearm slanting down to the bottom edge at
    bx; the cyan T-shirt sleeve shows at the bottom of the screen."""
    hw = w // 2
    c.poly([(hx, hy + 6), (hx + w, hy + 6), (bx + w + 4, H), (bx - 4, H)], 'skin', 0.5, seed=3 + side)
    c.poly([(bx - 6, H - 9), (bx + w + 6, H - 9), (bx + w + 8, H), (bx - 8, H)], 'sleeve', 0.5, seed=4 + side)
    c.rect(hx - 1, hy, hx + w + 1, hy + w, 'skin', 0.65, seed=5 + side)
    if grip:
        for k in range(1, 4): c.rect(hx + 1, hy + k * w // 4, hx + w - 1, hy + k * w // 4 + 1, 'skin', 0.25, seed=9, outline=False)


def weapon(key, frame):
    c = Canvas()
    cx = W // 2
    if key == 'fist':
        if frame == 0: limb(c, 82, 58, 26, 96, 1, False); limb(c, 14, 74, 22, 8, -1, False)
        elif frame == 1: limb(c, 48, 22, 34, 80, 1, False); limb(c, 10, 78, 22, 4, -1, False)
        else: limb(c, 64, 40, 30, 90, 1, False); limb(c, 12, 76, 22, 6, -1, False)
    elif key == 'chainsaw':
        j = [0, 2, -2][frame]; jy = [0, 1, -1][frame]
        c.poly([(cx - 7 + j, 4 + jy), (cx + 7 + j, 4 + jy), (cx + 13 + j, 52), (cx - 13 + j, 52)], 'stone', 0.6, seed=11)
        for t in range(6, 50, 5):
            o = (t // 5 + frame) % 2
            c.rect(cx - 12 + j + o, t + jy, cx - 9 + j + o, t + 3 + jy, 'steel', 0.15, seed=t, outline=False)
            c.rect(cx + 9 + j - o, t + jy, cx + 12 + j - o, t + 3 + jy, 'steel', 0.15, seed=t + 1, outline=False)
        c.poly([(cx - 26 + j, 48), (cx + 26 + j, 48), (cx + 32 + j, 96), (cx - 32 + j, 96)], 'redstone', 0.5, seed=12)
        c.rect(cx - 22 + j, 58, cx + 22 + j, 64, 'iron', 0.6, seed=13)
        c.rect(cx - 30 + j, 42, cx + 30 + j, 48, 'iron', 0.72, seed=14)
        limb(c, cx - 40 + j, 36, 16, cx - 52, -1); limb(c, cx + 22 + j, 64, 20, cx + 30, 1)
    elif key == 'pistol':
        dy = [0, 7, 3][frame]
        if frame == 1: c.flash(cx, 34 + dy, 15)
        c.poly([(cx - 6, 40 + dy), (cx + 6, 40 + dy), (cx + 12, 86 + dy), (cx - 12, 86 + dy)], 'steel', 0.6, seed=21)
        c.rect(cx - 3, 37 + dy, cx + 3, 42 + dy, 'iron', 0.8, seed=22)
        c.rect(cx - 9, 76 + dy, cx - 5, 80 + dy, 'iron', 0.7, seed=24); c.rect(cx + 5, 76 + dy, cx + 9, 80 + dy, 'iron', 0.7, seed=25)
        limb(c, cx - 15, 80 + dy, 30, cx - 16, 1)
    elif key == 'shotgun':
        dy = [0, 8, 3][frame]
        if frame == 1: c.flash(cx, 18 + dy, 17)
        c.poly([(cx - 4, 22 + dy), (cx + 4, 22 + dy), (cx + 9, 84 + dy), (cx - 9, 84 + dy)], 'steel', 0.55, seed=31)
        c.rect(cx - 2, 20 + dy, cx + 2, 24 + dy, 'obsidian', 0.4, seed=32)
        py = 44 + dy + (12 if frame == 2 else 0)
        c.poly([(cx - 11, py), (cx + 11, py), (cx + 13, py + 18), (cx - 13, py + 18)], 'oak', 0.55, seed=33)
        for k in range(3): c.rect(cx - 10, py + 4 + k * 5, cx + 10, py + 5 + k * 5, 'oak', 0.2, seed=35, outline=False)
        c.poly([(cx - 12, 80 + dy), (cx + 12, 80 + dy), (cx + 18, 96), (cx - 18, 96)], 'oak', 0.45, seed=34)
        limb(c, cx - 30, py + 2, 18, cx - 50, -1); limb(c, cx + 12, 82 + dy, 22, cx + 22, 1)
    elif key == 'super':
        dy = [0, 9, 16][frame]; tilt = 12 if frame == 2 else 0
        if frame == 1: c.flash(cx - 7, 16 + dy, 15); c.flash(cx + 7, 16 + dy, 15, seed=8)
        for s_ in (-1, 1):
            x0 = cx + s_ * 7
            c.poly([(x0 - 5, 20 + dy + tilt), (x0 + 5, 20 + dy + tilt), (x0 + 8, 78 + dy), (x0 - 8, 78 + dy)], 'steel', 0.55, seed=41 + s_)
            c.rect(x0 - 2, 18 + dy + tilt, x0 + 2, 23 + dy + tilt, 'obsidian', 0.3, seed=43)
        c.poly([(cx - 16, 70 + dy), (cx + 16, 70 + dy), (cx + 22, 96), (cx - 22, 96)], 'darkoak', 0.55, seed=44)
        limb(c, cx - 38, 52 + dy, 18, cx - 56, -1); limb(c, cx + 16, 80 + dy, 22, cx + 26, 1)
    elif key == 'chaingun':
        dy = [0, 4, 4][frame]
        if frame: c.flash(cx + (-4 if frame == 1 else 4), 12 + dy, 16, seed=frame)
        c.poly([(cx - 22, 52 + dy), (cx + 22, 52 + dy), (cx + 28, 96), (cx - 28, 96)], 'iron', 0.55, seed=51)
        c.rect(cx - 18, 60 + dy, cx + 18, 64 + dy, 'gold', 0.62, seed=52)
        for i in range(5):
            bx = cx - 12 + i * 6
            c.poly([(bx - 2, 16 + dy), (bx + 2, 16 + dy), (bx + 3, 54 + dy), (bx - 3, 54 + dy)], 'steel', 0.4 + 0.15 * ((i + frame) % 2), seed=53 + i)
        c.rect(cx - 17, 24 + dy, cx + 17, 29 + dy, 'iron', 0.65, seed=58)
        limb(c, cx - 44, 60 + dy, 20, cx - 56, -1); limb(c, cx + 24, 60 + dy, 20, cx + 36, 1)
    elif key == 'rocket':
        dy = [0, 10, 4][frame]
        if frame == 1: c.flash(cx, 16 + dy, 19)
        c.poly([(cx - 14, 20 + dy), (cx + 14, 20 + dy), (cx + 22, 96), (cx - 22, 96)], 'steel', 0.5, seed=61)
        c.ellipse(cx, 22 + dy, 11, 5, 'obsidian', 0.2, seed=62)
        for t in (36, 60): c.poly([(cx - 16, t + dy), (cx + 16, t + dy), (cx + 18, t + 6 + dy), (cx - 18, t + 6 + dy)], 'redstone', 0.55, seed=63 + t)
        c.rect(cx + 16, 44 + dy, cx + 26, 60 + dy, 'iron', 0.6, seed=64)
        limb(c, cx - 42, 58 + dy, 20, cx - 56, -1); limb(c, cx + 20, 70 + dy, 22, cx + 30, 1)
    elif key == 'plasma':
        dy = [0, 4, 4][frame]
        if frame: c.flash(cx, 14 + dy, 14 if frame == 1 else 10, 'soul', seed=frame)
        c.poly([(cx - 9, 18 + dy), (cx + 9, 18 + dy), (cx + 20, 96), (cx - 20, 96)], 'iron', 0.55, seed=71)
        for t in range(26, 80, 10): c.poly([(cx - 10, t + dy), (cx + 10, t + dy), (cx + 12, t + 5 + dy), (cx - 12, t + 5 + dy)], 'diamond', 0.78 if frame else 0.55, seed=72 + t)
        c.rect(cx - 4, 15 + dy, cx + 4, 20 + dy, 'soul', 0.7, seed=73)
        limb(c, cx - 38, 60 + dy, 18, cx - 54, -1); limb(c, cx + 20, 66 + dy, 20, cx + 32, 1)
    elif key == 'bfg':
        dy = [0, 2, 10][frame]
        c.poly([(cx - 26, 22 + dy), (cx + 26, 22 + dy), (cx + 38, 96), (cx - 38, 96)], 'obsidian', 0.55, seed=83)
        c.ellipse(cx, 26 + dy, 17, 7, 'emerald', 0.8 if frame else 0.5, seed=84)
        for s_ in (-1, 1): c.poly([(cx + s_ * 24, 40 + dy), (cx + s_ * 31, 40 + dy), (cx + s_ * 38, 92), (cx + s_ * 31, 92)], 'emerald', 0.55, seed=85 + s_)
        c.rect(cx - 11, 52 + dy, cx + 11, 68 + dy, 'gold', 0.6, seed=87)
        if frame == 1: c.ellipse(cx, 22 + dy, 13, 9, 'bfglow', 0.75, seed=81, outline=False)
        if frame == 2: c.flash(cx, 14 + dy, 24, 'bfglow', seed=82, spikes=10)
        limb(c, cx - 50, 62 + dy, 20, cx - 60, -1); limb(c, cx + 30, 62 + dy, 20, cx + 40, 1)
    elif key == 'unmaker':
        dy = [0, 3, 3][frame]
        c.poly([(cx - 10, 24 + dy), (cx + 10, 24 + dy), (cx + 24, 96), (cx - 24, 96)], 'nether', 0.5, seed=91)
        for s_ in (-1, 1):
            c.poly([(cx + s_ * 7, 32 + dy), (cx + s_ * 22, 8 + dy), (cx + s_ * 27, 12 + dy), (cx + s_ * 15, 36 + dy)], 'bone', 0.62, seed=92 + s_)
        c.ellipse(cx, 52 + dy, 7, 7, 'redstone', 0.85 if frame else 0.55, seed=94)
        c.rect(cx - 18, 70 + dy, cx + 18, 76 + dy, 'bone', 0.55, seed=95)
        if frame: c.flash(cx, 20 + dy, 12 if frame == 1 else 9, 'laser', seed=frame, spikes=6)
        limb(c, cx - 40, 60 + dy, 18, cx - 54, -1); limb(c, cx + 22, 66 + dy, 20, cx + 32, 1)
    return c


ITEM = {  # 16x16 pickup icons: side view, Minecraft item style (diagonal handled by simple poly)
    'fist': [('skin', [(4, 5), (12, 5), (12, 12), (4, 12)]), ('sleeve', [(5, 12), (11, 12), (11, 15), (5, 15)])],
    'chainsaw': [('stone', [(7, 2), (14, 2), (14, 6), (7, 6)]), ('redstone', [(2, 5), (9, 5), (9, 11), (2, 11)]), ('iron', [(3, 11), (6, 11), (6, 14), (3, 14)])],
    'pistol': [('steel', [(3, 5), (13, 5), (13, 8), (3, 8)]), ('darkoak', [(3, 8), (7, 8), (6, 14), (2, 14)])],
    'shotgun': [('steel', [(1, 6), (15, 6), (15, 8), (1, 8)]), ('oak', [(7, 8), (11, 8), (11, 10), (7, 10)]), ('oak', [(1, 8), (5, 8), (4, 12), (0, 12)])],
    'super': [('steel', [(2, 5), (15, 5), (15, 9), (2, 9)]), ('darkoak', [(1, 9), (6, 9), (5, 13), (0, 13)])],
    'chaingun': [('steel', [(6, 4), (15, 4), (15, 10), (6, 10)]), ('iron', [(1, 3), (7, 3), (7, 12), (1, 12)]), ('gold', [(2, 6), (6, 6), (6, 8), (2, 8)])],
    'rocket': [('steel', [(1, 5), (15, 5), (15, 10), (1, 10)]), ('redstone', [(5, 5), (7, 5), (7, 10), (5, 10)]), ('iron', [(9, 10), (11, 10), (11, 13), (9, 13)])],
    'plasma': [('iron', [(2, 5), (14, 5), (14, 10), (2, 10)]), ('diamond', [(4, 6), (12, 6), (12, 9), (4, 9)]), ('iron', [(4, 10), (7, 10), (6, 13), (3, 13)])],
    'bfg': [('obsidian', [(1, 3), (15, 3), (15, 12), (1, 12)]), ('emerald', [(11, 4), (15, 4), (15, 11), (11, 11)]), ('gold', [(5, 6), (9, 6), (9, 9), (5, 9)])],
    'unmaker': [('nether', [(2, 5), (12, 5), (12, 11), (2, 11)]), ('bone', [(11, 3), (15, 5), (12, 7)]), ('bone', [(11, 9), (15, 11), (12, 13)]), ('redstone', [(6, 7), (8, 7), (8, 9), (6, 9)])],
}

NAMES = {'fist': 'Fist', 'chainsaw': 'Chainsaw', 'pistol': 'Pistol', 'shotgun': 'Shotgun', 'super': 'Super Shotgun', 'chaingun': 'Chaingun',
         'rocket': 'Rocket Launcher', 'plasma': 'Plasma Rifle', 'bfg': 'BFG 9000', 'unmaker': 'Unmaker'}


def main():
    tex = os.path.join(ASSETS, 'textures', 'c64'); os.makedirs(tex, exist_ok=True)
    itex = os.path.join(ASSETS, 'textures', 'item'); os.makedirs(itex, exist_ok=True)
    models = os.path.join(ASSETS, 'models', 'item'); os.makedirs(models, exist_ok=True)
    lang = {}
    sheet = Image.new('RGBA', (W * 3 * 2, H * 10 * 2), (60, 60, 70, 255))
    for row, key in enumerate(NAMES):
        for f in range(3):
            im = weapon(key, f).image()
            im.save(os.path.join(tex, '%s_%d.png' % (key, f)))
            sheet.alpha_composite(im.resize((W * 2, H * 2), Image.NEAREST), (f * W * 2, row * H * 2))
        c = Canvas(16, 16)
        for i, (mat, pts) in enumerate(ITEM[key]): c.poly(pts, mat, 0.55, seed=i + 1, grain=1)
        c.image().save(os.path.join(itex, 'c64_%s.png' % key))
        json.dump({'parent': 'item/handheld', 'textures': {'layer0': 'crossover_rebuilt:item/c64_%s' % key}}, open(os.path.join(models, 'c64_%s.json' % key), 'w'))
        lang['item.crossover_rebuilt.c64_%s' % key] = NAMES[key]
    langdir = os.path.join(ASSETS, 'lang'); os.makedirs(langdir, exist_ok=True)
    lp = os.path.join(langdir, 'en_us.json')
    old = json.load(open(lp)) if os.path.exists(lp) else {}
    old.update(lang); json.dump(old, open(lp, 'w'), indent=1)
    if '--preview' in sys.argv: sheet.save(sys.argv[sys.argv.index('--preview') + 1])
    print('c64 art: %d weapons x 3 frames, %d item icons' % (len(NAMES), len(NAMES)))


if __name__ == '__main__':
    main()
