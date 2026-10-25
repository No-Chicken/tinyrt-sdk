"""Generate Pixel Quest atlas, palette, PCM effects and cover; Pillow only, no network.

All artwork and sounds are drawn/synthesized here from scratch, so the output is
fully reproducible from this file.
"""
from pathlib import Path
import math
import struct
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parent

# 调色板：索引 0 透明，其余为 RGB565 原色。
COLORS = {
    '.': None,
    'k': (34, 25, 43),     # 描边
    'w': (244, 244, 248),  # 白
    'l': (200, 208, 224),  # 浅灰
    'c': (63, 208, 255),   # 面罩青
    'C': (31, 127, 184),   # 深青
    'o': (255, 140, 46),   # 橙
    'O': (194, 83, 26),    # 深橙
    'y': (255, 214, 63),   # 黄
    'Y': (212, 150, 26),   # 深黄
    'g': (98, 212, 74),    # 草绿
    'G': (47, 143, 58),    # 深绿
    'd': (176, 106, 60),   # 土
    'D': (112, 60, 34),    # 深土
    'b': (204, 92, 52),    # 砖
    'B': (122, 46, 28),    # 深砖
    's': (154, 163, 181),  # 石
    'S': (92, 100, 120),   # 深石
    'p': (155, 91, 214),   # 甲虫紫
    'P': (93, 47, 143),    # 深紫
    'r': (232, 66, 74),    # 红
    'v': (255, 95, 200),   # 宝石粉
    'V': (176, 42, 138),   # 深粉
    'h': (143, 212, 122),  # 远山
    'H': (107, 184, 94),   # 远山暗
    'u': (214, 236, 255),  # 云影
    't': (66, 245, 200),   # 传送门
    'T': (26, 165, 138),   # 传送门暗
    'n': (160, 110, 70),   # 木板
    'N': (100, 64, 40),    # 木板暗
}
KEYS = [k for k in COLORS if k != '.']
INDEX = {'.': 0, **{k: i + 1 for i, k in enumerate(KEYS)}}
SKY = (110, 198, 255)


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def art(rows, w=16, h=16):
    assert len(rows) == h, (len(rows), rows)
    for r in rows:
        assert len(r) == w, (len(r), r)
    return rows


TILES = {}
# 1 草地顶
TILES[1] = art([
    "kkkkkkkkkkkkkkkk",
    "gggggggggggggggg",
    "gggGgggggggGgggg",
    "gGgGggGgggGgGggg",
    "GdGGGGdGGGdGGGdG",
    "dddDdddddddDdddd",
    "dddddddDdddddddd",
    "ddDdddddddddDddd",
    "dddddddddddddddd",
    "dddddDddddDddddd",
    "dddddddddddddddd",
    "dDdddddddddddDdd",
    "dddddddDdddddddd",
    "dddddddddddddddd",
    "ddddDddddddDdddd",
    "dddddddddddddddd",
])
# 2 土
TILES[2] = art([
    "dddddddddddddddd",
    "ddDdddddddDddddd",
    "dddddddddddddddd",
    "dddddDdddddddDdd",
    "dddddddddddddddd",
    "dDdddddddDdddddd",
    "dddddddddddddddd",
    "ddddddDddddddddd",
    "dddddddddddDdddd",
    "dddDdddddddddddd",
    "dddddddddddddddd",
    "ddddddddDddddDdd",
    "dDdddddddddddddd",
    "dddddddddddddddd",
    "ddddddDddddddddd",
    "dddddddddddddddd",
])
# 3 砖块
TILES[3] = art([
    "kkkkkkkkkkkkkkkk",
    "kbbbbbbkbbbbbbbk",
    "kbbbbbbkbbbbbbbk",
    "kBBBBBBkBBBBBBBk",
    "kkkkkkkkkkkkkkkk",
    "kbbbkbbbbbbbkbbk",
    "kbbbkbbbbbbbkbbk",
    "kBBBkBBBBBBBkBBk",
    "kkkkkkkkkkkkkkkk",
    "kbbbbbbkbbbbbbbk",
    "kbbbbbbkbbbbbbbk",
    "kBBBBBBkBBBBBBBk",
    "kkkkkkkkkkkkkkkk",
    "kbbbkbbbbbbbkbbk",
    "kBBBkBBBBBBBkBBk",
    "kkkkkkkkkkkkkkkk",
])
# 4 宝石箱（星标）
TILES[4] = art([
    "kkkkkkkkkkkkkkkk",
    "kyyyyyyyyyyyyyyk",
    "kyYyyyyyyyyyyYyk",
    "kyyyyyykkyyyyyyk",
    "kyyyyyykwkyyyyyk",
    "kyyyykkkwkkkyyyk",
    "kyyyykwwwwwkyyyk",
    "kyyyyykwwwkyyyyk",
    "kyyyyykwkwkyyyyk",
    "kyyyykwkykwkyyyk",
    "kyyyykkyyykkyyyk",
    "kyyyyyyyyyyyyyyk",
    "kyYyyyyyyyyyyYyk",
    "kyyyyyyyyyyyyyyk",
    "kYYYYYYYYYYYYYYk",
    "kkkkkkkkkkkkkkkk",
])
# 5 已用箱
TILES[5] = art([
    "kkkkkkkkkkkkkkkk",
    "kDDDDDDDDDDDDDDk",
    "kDdDDDDDDDDDDdDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kDdDDDDDDDDDDdDk",
    "kDDDDDDDDDDDDDDk",
    "kDDDDDDDDDDDDDDk",
    "kkkkkkkkkkkkkkkk",
])
# 6 石块
TILES[6] = art([
    "kkkkkkkkkkkkkkkk",
    "kllllllllllllllk",
    "klssssssssssssSk",
    "klssssssssssssSk",
    "klsslssssssssSSk",
    "klssssssssssssSk",
    "klssssssssssssSk",
    "klssssssSssssSSk",
    "klssssssssssssSk",
    "klssssssssssssSk",
    "klsssssssssslsSk",
    "klssssssssssssSk",
    "klssssssssssssSk",
    "klSSSSSSSSSSSSSk",
    "kSSSSSSSSSSSSSSk",
    "kkkkkkkkkkkkkkkk",
])
# 7 尖刺
TILES[7] = art([
    "................",
    "................",
    "................",
    "................",
    "................",
    "...k.......k....",
    "..klk.....klk...",
    "..klk.....klk...",
    ".klssk...klssk..",
    ".klssk...klssk..",
    "klsssSk.klsssSk.",
    "klsssSkkklsssSkk",
    "kkkkkkkkkkkkkkkk",
    "kSSSSSSSSSSSSSSk",
    "kSsSSSSsSSSSsSSk",
    "kkkkkkkkkkkkkkkk",
])
# 8 木平台
TILES[8] = art([
    "kkkkkkkkkkkkkkkk",
    "knnnnnnnnnnnnnnk",
    "knnNnnnnnnnNnnnk",
    "kNNNNNNNNNNNNNNk",
    "kkkkkkkkkkkkkkkk",
    ".kNk........kNk.",
    ".kNk........kNk.",
    "..k..........k..",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
])
# 9 灌木（装饰）
TILES[9] = art([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....kkkk.......",
    "...kkggggkkk....",
    "..kggggGggggk...",
    ".kgggGggggGggk..",
    ".kggggggggggggk.",
    "kgGgggggGgggggk.",
    "kggggGgggggGggk.",
    "kGGGGGGGGGGGGGGk",
    "kkkkkkkkkkkkkkkk",
])
# 10 小花（装饰）
TILES[10] = art([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....k......k....",
    "...kvk....kyk...",
    "..kvyvk..kywyk..",
    "...kvk....kyk...",
    "....g......g....",
    "...gGg..g.gGg...",
    "..g.g..gGg.g....",
    "....g...g..g....",
])
# 11 检查点旗（未激活）
TILES[11] = art([
    "..kk............",
    "..klkkkkk.......",
    "..klsssssk......",
    "..klssssssk.....",
    "..klsssssk......",
    "..klkkkkk.......",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    ".kkkk...........",
    "kSSSSk..........",
    "kkkkkk..........",
])
# 12 检查点旗（已激活）
TILES[12] = art([
    "..kk............",
    "..klkkkkk.......",
    "..klgggggk......",
    "..klgwgggGk.....",
    "..klgggggk......",
    "..klkkkkk.......",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    "..kl............",
    ".kkkk...........",
    "kSSSSk..........",
    "kkkkkk..........",
])
# 13 传送门上半
TILES[13] = art([
    "................",
    ".....kkkkkk.....",
    "...kkSSSSSSkk...",
    "..kSSkkkkkkSSk..",
    ".kSSkTTTTTTkSSk.",
    ".kSkTTttttTTkSk.",
    "kSSkTttwwttTkSSk",
    "kSkTttwttttTTkSk",
    "kSkTtwttttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTtttttwttTkSk",
    "kSkTttttttttTkSk",
    "kSkTttwtttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTtttttttwTkSk",
    "kSkTttttttttTkSk",
])
# 14 传送门下半
TILES[14] = art([
    "kSkTttttttttTkSk",
    "kSkTtttwttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTtwttttttTkSk",
    "kSkTttttttwtTkSk",
    "kSkTttttttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTtttwttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTttttttttTkSk",
    "kSkTTTTTTTTTTkSk",
    "kSSkkkkkkkkkkSSk",
    "kSSSSSSSSSSSSSSk",
    "kSsSSSSSSSSSSsSk",
    "kkkkkkkkkkkkkkkk",
])
# 15 宝石箱闪光帧
TILES[15] = art([
    "kkkkkkkkkkkkkkkk",
    "kwyyyyyyyyyyyyyk",
    "kywyyyyyyyyyyYyk",
    "kyyyyyykkyyyyyyk",
    "kyyyyyykwkyyyyyk",
    "kyyyykkkwkkkyyyk",
    "kyyyykwwwwwkyyyk",
    "kyyyyykwwwkyyyyk",
    "kyyyyykwkwkyyyyk",
    "kyyyykwkykwkyyyk",
    "kyyyykkyyykkyyyk",
    "kyyyyyyyyyyyyyyk",
    "kyYyyyyyyyyyywyk",
    "kyyyyyyyyyyyyywk",
    "kYYYYYYYYYYYYYYk",
    "kkkkkkkkkkkkkkkk",
])
# 16-19 宝石旋转四帧
TILES[16] = art([
    "................",
    "................",
    "......kkkk......",
    ".....kvwvvk.....",
    "....kvwvvvVk....",
    "...kvvvvvvvVk...",
    "...kvvvvvvvVk...",
    "...kVvvvvvVVk...",
    "....kVvvvVVk....",
    ".....kVvVVk.....",
    "......kVVk......",
    ".......kk.......",
    "................",
    "................",
    "................",
    "................",
])
TILES[17] = art([
    "................",
    "................",
    "......kkkk......",
    "......kwvk......",
    ".....kvwvVk.....",
    ".....kvvvVk.....",
    ".....kvvvVk.....",
    ".....kVvvVk.....",
    ".....kVvVVk.....",
    "......kvVk......",
    "......kVVk......",
    ".......kk.......",
    "................",
    "................",
    "................",
    "................",
])
TILES[18] = art([
    "................",
    "................",
    ".......kk.......",
    ".......kk.......",
    "......kwVk......",
    "......kvVk......",
    "......kvVk......",
    "......kvVk......",
    "......kvVk......",
    "......kVVk......",
    ".......kk.......",
    ".......kk.......",
    "................",
    "................",
    "................",
    "................",
])
TILES[19] = TILES[17]

# 24-28 主角：小探险机器人「豆豆」（原创），朝右。
PLAYER = {}
PLAYER[24] = art([
    "................",
    "........ky......",
    "........k.......",
    ".....kkkkkk.....",
    "....kwwwwwwk....",
    "...kwwwwwwwwk...",
    "...kwwkkkkkkk...",
    "...kwkcccccCk...",
    "...kwkcwcccCk...",
    "...kwwkkkkkkk...",
    "....kwwwwwwk....",
    "...kkoooooOkk...",
    "..kwkoyyyoOkwk..",
    "..kkkooooOOkkk..",
    "....kOOk.kOOk...",
    "....kkkk.kkkk...",
])
PLAYER[25] = art([
    "................",
    "........ky......",
    "........k.......",
    ".....kkkkkk.....",
    "....kwwwwwwk....",
    "...kwwwwwwwwk...",
    "...kwwkkkkkkk...",
    "...kwkcccccCk...",
    "...kwkcwcccCk...",
    "...kwwkkkkkkk...",
    "....kwwwwwwk....",
    "...kkoooooOkk...",
    "..kwkoyyyoOkwk..",
    "..kkkooooOOkkk..",
    "...kOOk..kOOk...",
    "...kkkk...kkk...",
])
PLAYER[26] = art([
    "........ky......",
    "........k.......",
    ".....kkkkkk.....",
    "....kwwwwwwk....",
    "...kwwwwwwwwk...",
    "...kwwkkkkkkk...",
    "...kwkcccccCk...",
    "...kwkcwcccCk...",
    "...kwwkkkkkkk...",
    "....kwwwwwwk....",
    "...kkoooooOkk...",
    "..kwkoyyyoOkwk..",
    "..kkkooooOOkkk..",
    ".....kOOkOOk....",
    ".....kkkkkkk....",
    "................",
])
PLAYER[27] = art([
    "........ky......",
    "........k.......",
    ".....kkkkkk.....",
    "....kwwwwwwk....",
    "...kwwwwwwwwk.k.",
    "...kwwkkkkkkkkwk",
    "...kwkcccccCkkwk",
    "...kwkcwcccCk.k.",
    "...kwwkkkkkkk...",
    "....kwwwwwwk....",
    "..kkkoooooOk....",
    ".kwkkoyyyoOk....",
    ".kkk.ooooOOk....",
    "....kOOkkOOk....",
    "...kOOk..kkk....",
    "...kkk..........",
])
PLAYER[28] = art([
    "................",
    "................",
    ".....kkkkkk.....",
    "....kwwwwwwk....",
    "...kwwwwwwwwk...",
    "...kwwkkkkkkk...",
    "...kwkrkCrkCk...",
    "...kwkkrkkrkk...",
    "...kwkrkCrkCk...",
    "...kwwkkkkkkk...",
    "....kwwwwwwk....",
    "..kwkoooooOkwk..",
    "..kkkoyyyoOkkk..",
    "....kooooOOk....",
    "....kOOkkOOk....",
    "....kkkkkkkk....",
])
# 32-34 敌人：圆甲虫（原创）
ENEMY = {}
ENEMY[32] = art([
    "................",
    "................",
    "................",
    ".....kkkkkk.....",
    "....kppppppk....",
    "...kppwpppppk...",
    "..kpwwpppPppPk..",
    "..kppppPppppPk..",
    ".kpppPpppPpppPk.",
    ".kkkkkkkkkkkkkk.",
    ".kwkwkPPPPkwkwk.",
    ".kkkkPPPPPPkkkk.",
    "..kPPPPPPPPPPk..",
    "...kkkkkkkkkk...",
    "...kk..kk..kk...",
    "..kk..kk..kk....",
])
ENEMY[33] = art([
    "................",
    "................",
    "................",
    ".....kkkkkk.....",
    "....kppppppk....",
    "...kppwpppppk...",
    "..kpwwpppPppPk..",
    "..kppppPppppPk..",
    ".kpppPpppPpppPk.",
    ".kkkkkkkkkkkkkk.",
    ".kwkwkPPPPkwkwk.",
    ".kkkkPPPPPPkkkk.",
    "..kPPPPPPPPPPk..",
    "...kkkkkkkkkk...",
    "....kk..kk..kk..",
    ".....kk..kk..kk.",
])
ENEMY[34] = art([
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "....kkkkkkkk....",
    "..kkppwpppPpkk..",
    ".kpppppPppppPPk.",
    ".kkkkkkkkkkkkkk.",
    ".kwkwkPPPPkwkwk.",
    ".kkkkkkkkkkkkkk.",
])
# 36-37 云（32x16），38-39 远山（32x16）
CLOUD = art([
    "................................",
    "................................",
    "............wwwwww..............",
    "..........wwwwwwwwww............",
    "........wwwwwwwwwwwww...........",
    "....wwwwwwwwwwwwwwwwwwwww.......",
    "...wwwwwwwwwwwwwwwwwwwwwwww.....",
    "..wwwwwwwwwwwwwwwwwwwwwwwwwww...",
    ".wwwwwwwwwwwwwwwwwwwwwwwwwwwww..",
    ".wwwwwwwwwwwwwwwwwwwwwwwwwwwwww.",
    ".uwwwwwwwwwwwwwwwwwwwwwwwwwwwwu.",
    "..uuwwwwwwwwwwwwwwwwwwwwwwwwuu..",
    "....uuuuuuuuuuuuuuuuuuuuuuuu....",
    "................................",
    "................................",
    "................................",
], 32, 16)
HILL = art([
    "................................",
    "................................",
    "................................",
    "................................",
    "...........hhhhhhhhhh...........",
    "........hhhhhhhhhhhhhhhh........",
    "......hhhhhhhhhhhhhhhhhhhh......",
    ".....hhhhhhhhhhhhhhhhhhhhhh.....",
    "....hhhhhhhHhhhhhhhhhhhhhhhh....",
    "...hhhhhhhhhhhhhhhhhhhHhhhhhh...",
    "..hhhhHhhhhhhhhhhhhhhhhhhhhhhh..",
    ".hhhhhhhhhhhhhhhHhhhhhhhhhhhhhh.",
    ".hhhhhhhhhhhhhhhhhhhhhhhhhhhhhh.",
    "hhhhhhhhhhHhhhhhhhhhhhhhhHhhhhhh",
    "hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh",
    "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH",
], 32, 16)

# 8x8 字形：0-9、x、心、宝石、星
GLYPHS = [
    ["..kkkk..", ".kwwwwk.", "kwwkkwwk", "kwwkkwwk", "kwwkkwwk", "kwwkkwwk", ".kwwwwk.", "..kkkk.."],
    ["...kk...", "..kwwk..", ".kwwwk..", "..kwwk..", "..kwwk..", "..kwwk..", ".kwwwwk.", "..kkkk.."],
    ["..kkkk..", ".kwwwwk.", "kwkkkwwk", "..kkwwk.", ".kwwwk..", "kwwkkkk.", "kwwwwwwk", ".kkkkkk."],
    [".kkkkk..", "kwwwwwk.", ".kkkkwwk", "..kwwwk.", "..kkkwwk", ".kkkkwwk", "kwwwwwk.", ".kkkkk.."],
    ["....kk..", "...kwwk.", "..kwwwk.", ".kwkwwk.", "kwwwwwwk", ".kkkwwk.", "...kwwk.", "....kk.."],
    [".kkkkkk.", "kwwwwwwk", "kwwkkkk.", "kwwwwwk.", ".kkkkwwk", ".kkkkwwk", "kwwwwwk.", ".kkkkk.."],
    ["..kkkk..", ".kwwwwk.", "kwwkkkk.", "kwwwwwk.", "kwwkkwwk", "kwwkkwwk", ".kwwwwk.", "..kkkk.."],
    [".kkkkkk.", "kwwwwwwk", ".kkkkwwk", "...kwwk.", "..kwwk..", "..kwwk..", "..kwwk..", "...kk..."],
    ["..kkkk..", ".kwwwwk.", "kwwkkwwk", ".kwwwwk.", "kwwkkwwk", "kwwkkwwk", ".kwwwwk.", "..kkkk.."],
    ["..kkkk..", ".kwwwwk.", "kwwkkwwk", "kwwkkwwk", ".kwwwwwk", "..kkkwwk", ".kwwwwk.", "..kkkk.."],
    ["........", "........", "kk...kk.", "kwk.kwk.", ".kwkwk..", ".kwkwk..", "kwk.kwk.", "kk...kk."],
    [".kk.kk..", "krrkrrk.", "krrrrrrk", "krrrrrrk", ".krrrrk.", "..krrk..", "...kk...", "........"],
    ["..kkkk..", ".kvwvvk.", "kvwvvvVk", "kvvvvvVk", ".kVvvVk.", "..kVVk..", "...kk...", "........"],
    ["...kk...", "...kyk..", "kkkkykkk", "kyyyyyyk", ".kyyyyk.", ".kykkyk.", "kyk..kyk", "kk....kk"],
]


def build_atlas():
    atlas = Image.new('P', (128, 128), 0)
    px = atlas.load()

    def put(rows, cx, cy):
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch != '.':
                    px[cx + x, cy + y] = INDEX[ch]

    def cell(i):
        return (i % 8) * 16, (i // 8) * 16
    for i, rows in {**TILES, **PLAYER, **ENEMY}.items():
        put(rows, *cell(i))
    put(CLOUD, *cell(36))
    put(HILL, *cell(38))
    for g, rows in enumerate(GLYPHS):
        put(rows, (g % 16) * 8, 80 + (g // 16) * 8)
    return atlas


def palette_list():
    return [0] + [rgb565(COLORS[k]) for k in KEYS]


def preview_png(atlas, path, scale=4):
    img = Image.new('RGB', atlas.size, SKY)
    src = atlas.load()
    out = img.load()
    for y in range(atlas.height):
        for x in range(atlas.width):
            i = src[x, y]
            if i:
                out[x, y] = COLORS[KEYS[i - 1]]
    img.resize((atlas.width * scale, atlas.height * scale), Image.Resampling.NEAREST).save(path)


# ---- 音效：16 kHz PCM16 单声道，方波/噪声合成 ----
RATE = 16000


def tone(segments, volume=0.32, duty=0.5):
    """segments: [(start_hz, end_hz, seconds)]，带线性滑音与指数衰减包络。"""
    out = []
    phase = 0.0
    for f0, f1, dur in segments:
        n = int(RATE * dur)
        for i in range(n):
            t = i / max(1, n - 1)
            f = f0 + (f1 - f0) * t
            phase = (phase + f / RATE) % 1.0
            env = (1.0 - t) ** 1.5 if dur > 0.05 else 1.0 - t
            attack = min(1.0, i / 80.0)
            s = (1.0 if phase < duty else -1.0) * volume * env * attack
            out.append(int(s * 32767))
    return out


def noise(dur, volume=0.3, seed=7):
    out = []
    n = int(RATE * dur)
    x = seed
    held = 0
    for i in range(n):
        if i % 6 == 0:
            x = (x * 1103515245 + 12345) & 0x7fffffff
            held = ((x >> 16) & 1) * 2 - 1
        t = i / n
        out.append(int(held * volume * (1 - t) ** 2 * 32767))
    return out


def mix(a, b):
    n = max(len(a), len(b))
    a = a + [0] * (n - len(a))
    b = b + [0] * (n - len(b))
    return [max(-32767, min(32767, x + y)) for x, y in zip(a, b)]


SOUNDS = {
    'JUMP': tone([(330, 880, 0.14)], 0.22, 0.25),
    'GEM': tone([(1320, 1320, 0.06), (1760, 1760, 0.16)], 0.20, 0.5),
    'STOMP': mix(tone([(220, 80, 0.10)], 0.30, 0.5), noise(0.08, 0.18)),
    'BUMP': tone([(160, 110, 0.07)], 0.30, 0.5),
    'HURT': tone([(660, 600, 0.10), (520, 400, 0.12), (400, 300, 0.14), (300, 110, 0.30)], 0.24, 0.5),
    'CLEAR': tone([(523, 523, 0.10), (659, 659, 0.10), (784, 784, 0.10), (1046, 1046, 0.32)], 0.22, 0.5),
    'CHECK': tone([(784, 784, 0.07), (1175, 1175, 0.14)], 0.20, 0.25),
}


def pcm(samples):
    return struct.pack('<' + 'h' * len(samples), *samples)


# ---- 封面 210x210 ----
def cover(atlas):
    pal = {i + 1: COLORS[k] for i, k in enumerate(KEYS)}
    src = atlas.load()
    img = Image.new('RGB', (105, 105), SKY)
    out = img.load()

    def blit(u, v, w, h, x, y):
        for yy in range(h):
            for xx in range(w):
                i = src[u + xx, v + yy]
                if i and 0 <= x + xx < 105 and 0 <= y + yy < 105:
                    out[x + xx, y + yy] = pal[i]
    blit(96, 64, 32, 16, -6, 58)
    blit(96, 64, 32, 16, 64, 54)
    blit(64, 64, 32, 16, 8, 14)
    blit(64, 64, 32, 16, 62, 26)
    for x in range(-4, 105, 16):
        blit(16, 0, 16, 16, x, 74)
        blit(32, 0, 16, 16, x, 90)
    blit(48, 0, 16, 16, 30, 30)
    blit(64, 0, 16, 16, 46, 30)
    blit(48, 0, 16, 16, 62, 30)
    blit(0, 32, 16, 16, 47, 15)
    blit(48, 48, 16, 16, 30, 46)
    blit(0, 64, 16, 16, 70, 58)
    blit(32, 48, 16, 16, 84, 58)
    return img.resize((210, 210), Image.Resampling.NEAREST)


def main():
    atlas = build_atlas()
    pal = palette_list()
    data = bytearray()
    defs = ['/* Generated by prepare_assets.py; do not edit. */']
    defs.append('#define ATLAS_OFFSET %du' % len(data))
    data += atlas.tobytes()
    defs.append('#define ATLAS_LENGTH %du' % (128 * 128))
    defs.append('#define PALETTE_OFFSET %du' % len(data))
    defs.append('#define PALETTE_COUNT %du' % len(pal))
    data += struct.pack('<' + 'H' * len(pal), *pal)
    for name, samples in SOUNDS.items():
        if len(data) % 2:
            data.append(0)
        raw = pcm(samples)
        assert 2 <= len(raw) <= 32000 and len(raw) % 2 == 0, name
        defs.append('#define SOUND_%s_OFFSET %du' % (name, len(data)))
        defs.append('#define SOUND_%s_LENGTH %du' % (name, len(raw)))
        data += raw
    (ROOT / 'resources.bin').write_bytes(bytes(data))
    (ROOT / 'assets_generated.h').write_text('\n'.join(defs) + '\n', encoding='utf-8', newline='\n')
    cover(atlas).save(ROOT / 'cover.png', optimize=True)
    if '--preview' in sys.argv:
        preview_png(atlas, ROOT / 'build' / 'atlas-preview.png')
    print('resources.bin', len(data), 'bytes; palette', len(pal), 'colors')


if __name__ == '__main__':
    (ROOT / 'build').mkdir(exist_ok=True)
    main()
