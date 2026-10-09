#!/usr/bin/env python3
"""make-icon.py TEXDIR OUT.png [SIZE] - launcher icon in the style of the 2017-era Minecraft launcher: an isometric grass
block drawn texel by texel from 1.12's own textures (grass tinted with the plains colour from the colormap)."""
import sys
from PIL import Image, ImageDraw

tex, out = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 192
blk = tex + '/assets/minecraft/textures/blocks/'
rgba = lambda n: Image.open(blk + n).convert('RGBA')
tint = Image.open(tex + '/assets/minecraft/textures/colormap/grass.png').convert('RGB').getpixel((51, 173))  # plains

def tinted(img):
    px = img.load()
    for y in range(16):
        for x in range(16):
            r, g, b, a = px[x, y]
            px[x, y] = (r * tint[0] // 255, g * tint[1] // 255, b * tint[2] // 255, a)
    return img

top = tinted(rgba('grass_top.png'))
side = rgba('grass_side.png')
side.alpha_composite(tinted(rgba('grass_side_overlay.png')))

S = 4                      # supersampling, then a smooth downscale
W = size * S
m = W * 0.06               # margin
w = W - 2 * m              # cube width
a = (w / 2, w / 4)         # right-down edge
b = (-w / 2, w / 4)        # left-down edge
d = (0, w / 2 + 0.0)       # down edge
T = (W / 2, m + (W - 2 * m - (w / 2 + w / 2)) / 2)   # top vertex, centred vertically
img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
dr = ImageDraw.Draw(img)

def face(texture, origin, eu, ev, shade):
    px = texture.load()
    for v in range(16):
        for u in range(16):
            r, g, bb, al = px[u, v]
            if not al:
                continue
            pts = []
            for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                uu, vv = (u + du) / 16, (v + dv) / 16
                pts.append((origin[0] + uu * eu[0] + vv * ev[0], origin[1] + uu * eu[1] + vv * ev[1]))
            dr.polygon(pts, fill=(int(r * shade), int(g * shade), int(bb * shade), 255))

face(top, T, a, b, 1.0)
L = (T[0] + b[0], T[1] + b[1])
face(side, L, a, d, 0.8)                                   # front-left
C = (T[0] + a[0] + b[0], T[1] + a[1] + b[1])
face(side, C, (-b[0], -b[1]), d, 0.6)                      # front-right
img.resize((size, size), Image.LANCZOS).save(out)
print(out, size)
