"""Generate the WAVE SIMULATOR cover: pixel waves in the middle, pixel title below.
Logical canvas 105x105, upscaled to 210x210 with nearest-neighbour."""
import math, random
from PIL import Image

import sys, os
N = 105
K = N / 120  # layout designed on 120, scaled here
OUT = sys.argv[1] if len(sys.argv) > 1 else "."
random.seed(7)

# 5x7 bitmap font (own glyphs)
FONT = {
    'W': ["10001","10001","10001","10101","10101","11011","10001"],
    'A': ["01110","10001","10001","11111","10001","10001","10001"],
    'V': ["10001","10001","10001","10001","01010","01010","00100"],
    'E': ["11111","10000","10000","11110","10000","10000","11111"],
    'S': ["01111","10000","10000","01110","00001","00001","11110"],
    'I': ["11111","00100","00100","00100","00100","00100","11111"],
    'M': ["10001","11011","10101","10101","10001","10001","10001"],
    'U': ["10001","10001","10001","10001","10001","10001","01110"],
    'L': ["10000","10000","10000","10000","10000","10000","11111"],
    'T': ["11111","00100","00100","00100","00100","00100","00100"],
    'O': ["01110","10001","10001","10001","10001","10001","01110"],
    'R': ["11110","10001","10001","11110","10100","10010","10001"],
}

BG_TOP = (8, 10, 30)
BG_BOT = (14, 22, 52)
DEEP = (10, 40, 110)
MID = (20, 90, 190)
SHALLOW = (40, 170, 230)
CREST = (130, 235, 255)
FOAM = (240, 255, 255)
TXT = (255, 255, 255)
TXT_SHADOW = (20, 90, 190)
SUB = (130, 235, 255)

img = Image.new("RGB", (N, N))
px = img.load()

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

# background with banded (dithered) gradient
for y in range(N):
    for x in range(N):
        t = y / (N - 1)
        t = round(t * 6) / 6  # posterize into bands
        px[x, y] = lerp(BG_TOP, BG_BOT, t)

# stars
for _ in range(26):
    x, y = random.randrange(N), random.randrange(int(4*K), int(38*K))
    px[x, y] = random.choice([(90, 110, 170), (160, 180, 230), (255, 255, 255)])

# waves: three layers, back to front
def surface(x, base, amp, k, ph):
    return base + amp * math.sin(k * x + ph) + amp * 0.45 * math.sin(k * 2.3 * x + ph * 1.7)

layers = [
    (42*K, 5.0*K, 0.080/K, 0.6, (16, 60, 150)),
    (51*K, 6.0*K, 0.066/K, 2.4, MID),
    (60*K, 8.0*K, 0.055/K, 4.4, SHALLOW),
]
for li, (base, amp, k, ph, col) in enumerate(layers):
    for x in range(N):
        s = int(round(surface(x, base, amp, k, ph)))
        for y in range(s, int(76*K)):
            depth = (y - s) / (18*K)
            c = lerp(col, (8, 30, 90), min(depth, 1))
            # checker dither inside body for texture
            if (x + y) % 2 == 0 and depth > 0.3:
                c = lerp(c, (6, 28, 80), 0.25)
            px[x, y] = c
        # crest line
        px[x, s] = CREST if li < 2 else FOAM
        if li == 2:
            px[x, s + 1] = CREST
    # foam spray on the peaks of the front layer
    if li == 2:
        for x in range(N):
            s = surface(x, base, amp, k, ph)
            ds = surface(x + 1, base, amp, k, ph) - s
            if abs(ds) < 0.35 and s < base - amp * 0.5:
                for _ in range(5):
                    fx = x + random.randint(-2, 2)
                    fy = int(s) - random.randint(2, 6)
                    if 0 <= fx < N and 0 <= fy < N:
                        px[fx, fy] = FOAM

# floor band under the waves
for y in range(int(76*K), int(80*K)):
    for x in range(N):
        px[x, y] = (6, 20, 60) if y < int(80*K) - 1 else (40, 170, 230)

def text(s, cx, y, scale, col, shadow=None):
    w = len(s) * 6 * scale - scale
    x0 = cx - w // 2
    for i, ch in enumerate(s):
        g = FONT.get(ch)
        if not g:
            continue
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == "1":
                    for sy in range(scale):
                        for sx in range(scale):
                            X = x0 + (i * 6 + gx) * scale + sx
                            Y = y + gy * scale + sy
                            if shadow:
                                px[X, Y + scale] = shadow
                            px[X, Y] = col
    # second pass so shadows never overwrite glyph pixels
    if shadow:
        text(s, cx, y, scale, col)

text("WAVE", N // 2, int(84*K), 2, TXT, TXT_SHADOW)
text("SIMULATOR", N // 2, int(103*K), 1, SUB)

cover = img.resize((N * 2, N * 2), Image.NEAREST)  # 210x210
os.makedirs(OUT, exist_ok=True)
path = os.path.join(OUT, "cover.png")
cover.save(path, optimize=True)
print(path, os.path.getsize(path), "bytes")
