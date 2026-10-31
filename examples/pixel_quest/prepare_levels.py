"""Generate Pixel Quest's eight original stages into levels_generated.h.

Rows are built from small helpers so widths stay exact, then stored as runs. Reachability is proved by
the search solver in tests/pixel_quest, not by these limits alone.
Tiles: # ground  B brick  Q gem box  H shield box  S stone  = plank  ^ spike
L lava  u spring  o gem  b bush  f flower  C checkpoint  P portal  p boss portal
e beetle  a wasp  h hopper  x spiker  M/V moving platform  K boss  @ start
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ROWS = 14
GROUND = 11
MEADOW, CAVE, CLOUD, FORGE = range(4)


class Level:
    def __init__(self, name, theme, width):
        self.name, self.theme, self.width = name, theme, width
        self.cells = [[' '] * width for _ in range(ROWS)]

    def put(self, x, y, text):
        for i, ch in enumerate(text):
            if ch != ' ':
                self.cells[y][x + i] = ch
        return self

    def ground(self, a, b, top=GROUND, ch='#'):
        for y in range(top, ROWS):
            for x in range(a, b + 1):
                self.cells[y][x] = ch
        return self

    def lava(self, a, b):
        for x in range(a, b + 1):
            self.cells[ROWS - 2][x] = 'L'
        return self

    def stairs(self, x, height, descend=False):
        for i in range(height):
            size = height - i if descend else i + 1
            for y in range(GROUND - size, GROUND):
                self.cells[y][x + i] = 'S'
        return self

    def roof(self, a, b, depth=3):
        for y in range(depth):
            for x in range(a, b + 1):
                self.cells[y][x] = 'S'
        return self

    def arc(self, x, y, count=5):
        for i in range(count):
            lift = min(i, count - 1 - i)
            self.put(x + i, y - lift, 'o')
        return self

    def rows(self):
        return [''.join(row) for row in self.cells]

    def runs(self):
        """Horizontal same-tile runs; '#' splits into grass top and dirt below."""
        result = []
        rows = self.rows()
        for y, row in enumerate(rows):
            x = 0
            while x < self.width:
                ch = row[x]
                if ch == '#':
                    ch = 'D' if y and rows[y - 1][x] == '#' else 'G'
                end = x + 1
                while end < self.width and end - x < 255 and row[end] == row[x] and (
                        row[x] != '#' or ('D' if y and rows[y - 1][end] == '#' else 'G') == ch):
                    end += 1
                if ch != ' ':
                    # Actors occupy one cell each even when written side by side.
                    if ch in 'eahxMVK@CPp':
                        result.extend((n, y, 1, ch) for n in range(x, end))
                    else:
                        result.append((x, y, end - x, ch))
                x = end
        return result


def stage_1_1():
    l = Level('GREEN START', MEADOW, 168).ground(0, 34).ground(38, 74).ground(79, 118).ground(122, 167)
    l.put(3, 10, '@').put(8, 10, 'b').put(12, 10, 'f f').put(22, 10, 'e').put(30, 10, 'f')
    l.arc(13, 8).put(18, 7, 'BQBHB').arc(35, 9, 4).put(44, 10, 'e').put(50, 7, 'QBQ').put(58, 10, 'e   e')
    l.put(64, 8, '====').arc(64, 6, 4).arc(75, 8, 4).put(84, 10, 'C').stairs(92, 3).stairs(95, 3, True)
    l.put(102, 10, 'e').put(108, 7, 'BQB').arc(118, 8, 4).put(128, 10, '^^').put(135, 10, 'e   e')
    l.stairs(146, 4).stairs(150, 4, True).put(162, 10, 'P')
    return l


def stage_1_2():
    l = Level('WINDY STEPS', MEADOW, 180).ground(0, 22).ground(27, 52).ground(56, 88).ground(93, 128).ground(132, 179)
    l.put(3, 10, '@').put(12, 6, 'a').arc(14, 8).put(19, 10, 'e').put(22, 7, '===').arc(23, 8, 4)
    l.stairs(32, 4).put(36, 10, 'u').put(40, 3, 'ooooo').put(40, 4, '=====').put(46, 10, 'e').put(49, 6, 'a')
    l.put(60, 7, 'BHB').put(66, 10, 'e  e').put(72, 10, 'u').put(72, 1, 'ooo').put(76, 5, '====').put(81, 10, '^^^')
    l.arc(87, 7, 5).put(98, 10, 'C').put(104, 6, 'a').put(110, 6, 'a').put(106, 8, 'ooo').stairs(114, 3).put(117, 10, 'h')
    l.stairs(120, 3, True).arc(127, 7, 6).put(137, 10, 'e  h').put(146, 7, 'QBQ').put(152, 6, 'a')
    l.stairs(158, 5).stairs(163, 5, True).put(173, 10, 'P')
    return l


def stage_2_1():
    l = Level('ECHO CAVE', CAVE, 176).ground(0, 28).ground(33, 60).ground(68, 100).ground(106, 138).ground(143, 175)
    l.roof(0, 175, 2).roof(14, 26, 5).roof(46, 54, 5).roof(110, 124, 5)
    l.put(3, 10, '@').put(10, 10, 'e').arc(15, 8).put(21, 10, 'x').put(35, 7, 'BHB').put(40, 10, 'e').put(49, 10, 'x')
    l.put(54, 10, '^^').put(62, 7, 'M').put(61, 5, 'ooo').put(72, 10, 'e').put(78, 7, 'QBQ').put(84, 10, 'C')
    l.stairs(88, 3).put(93, 10, 'x').stairs(96, 3, True).arc(100, 8).put(113, 10, 'h').put(119, 10, 'x').put(128, 10, 'e')
    l.put(133, 7, '===').arc(138, 8, 5).put(148, 10, 'e x').put(155, 7, 'BQB').stairs(160, 4).stairs(164, 4, True).put(171, 10, 'P')
    return l


def stage_2_2():
    l = Level('CRYSTAL SHAFT', CAVE, 186).ground(0, 18).ground(30, 46).ground(60, 80).ground(92, 118).ground(128, 150).ground(160, 185)
    l.roof(0, 185, 2).put(3, 10, '@').put(10, 10, 'e').put(14, 7, 'QHQ').put(23, 8, 'M').arc(22, 6)
    l.put(34, 10, 'x').put(40, 10, 'u').put(38, 2, 'ooooo').put(37, 3, '=======').put(46, 3, '===').put(51, 7, 'V').put(50, 3, 'ooo')
    l.put(64, 10, 'e h').put(70, 7, 'BQB').put(76, 10, 'x').put(85, 8, 'M').put(84, 6, 'ooo').put(96, 10, 'C')
    l.put(102, 10, 'h').put(107, 7, '===').put(112, 10, 'x').put(122, 7, 'V').put(121, 3, 'ooo').put(134, 10, 'e')
    l.put(140, 10, 'u').put(138, 3, '=======').put(138, 2, 'ooooooo').put(155, 8, 'M').put(166, 10, 'x').put(172, 7, 'QBQ').put(180, 10, 'P')
    return l


def island(l, a, b, top):
    return l.ground(a, b, top)


def stage_3_1():
    l = Level('CLOUD HOP', CLOUD, 182)
    island(l, 0, 14, 11).put(3, 10, '@')
    island(l, 19, 28, 10).put(24, 9, 'e').arc(20, 7)
    island(l, 33, 40, 9).put(37, 5, 'a').put(36, 6, 'BHB')
    l.put(47, 8, 'M').put(46, 6, 'ooo')
    island(l, 56, 68, 10).put(62, 9, 'e').put(67, 9, 'u').put(66, 3, 'ooo')
    island(l, 73, 82, 7).put(76, 6, 'h').put(81, 3, 'a')
    island(l, 87, 96, 10).put(90, 9, 'C')
    l.put(101, 8, 'M').put(100, 6, 'ooo').put(106, 5, 'a')
    island(l, 109, 120, 9).put(116, 8, 'e').put(115, 5, 'QBQ')
    island(l, 125, 131, 11).put(130, 10, 'u').put(127, 7, 'a')
    island(l, 136, 144, 6).put(139, 5, 'h').arc(137, 3)
    l.put(148, 8, 'V').put(147, 3, 'ooo')
    island(l, 154, 181, 10).put(164, 9, 'e  e').stairs(170, 1).put(175, 9, 'P')
    return l


def stage_3_2():
    l = Level('STORM BRIDGE', CLOUD, 188)
    island(l, 0, 12, 10).put(3, 9, '@').put(16, 9, '=========').arc(17, 7).put(20, 8, 'a')
    island(l, 28, 36, 8).put(31, 7, 'h').put(30, 4, 'BHB')
    l.put(41, 8, '=====').put(44, 5, 'a').put(49, 7, 'V').put(48, 3, 'ooo')
    island(l, 57, 66, 9).put(60, 8, 'e').put(64, 8, 'x').put(70, 7, '===').put(76, 6, '===').put(82, 5, '===').arc(82, 3, 3)
    island(l, 88, 98, 8).put(91, 7, 'C').put(95, 7, 'h').put(104, 8, 'M').put(103, 6, 'ooo')
    island(l, 112, 122, 10).put(118, 9, 'x').put(121, 9, 'u').put(120, 3, 'ooo').put(126, 5, '========').put(129, 4, 'a')
    l.put(137, 8, 'V').put(136, 3, 'ooo')
    island(l, 143, 154, 9).put(150, 8, 'h').put(159, 8, 'M').put(158, 6, 'ooo')
    island(l, 168, 187, 10).put(174, 9, 'e').put(178, 6, 'QBQ').put(183, 9, 'P')
    return l


def stage_4_1():
    l = Level('FORGE GATE', FORGE, 184).ground(0, 20).ground(26, 44).ground(56, 76).ground(87, 113).ground(123, 142).ground(150, 183)
    for a, b in ((21, 25), (45, 55), (77, 86), (114, 122), (143, 149)):
        l.lava(a, b)
    l.put(3, 10, '@').put(10, 10, 'e').put(14, 7, 'BHB').put(30, 10, 'x').put(36, 10, 'h').put(41, 7, '===')
    l.put(48, 8, 'M').put(47, 6, 'ooo').put(62, 10, 'e x').put(70, 7, 'QBQ').put(80, 8, 'M').put(79, 6, 'ooo')
    l.put(95, 10, 'C').put(100, 10, 'x').put(104, 10, 'h').put(109, 6, 'a').put(117, 8, 'V').put(116, 3, 'ooo')
    l.stairs(127, 3).put(131, 10, 'h').stairs(134, 3, True).put(146, 8, '===').arc(145, 6, 4)
    l.put(155, 10, 'x').put(160, 10, 'e  h').put(168, 7, 'BQB').stairs(172, 4).put(179, 10, 'P')
    return l


def stage_4_2():
    l = Level('CORE GOLEM', FORGE, 96).ground(0, 26).ground(36, 95).lava(27, 35).roof(0, 95, 1)
    l.put(3, 10, '@').put(9, 7, 'BHB').put(14, 10, 'h').put(20, 10, 'x').put(29, 8, 'M').put(28, 6, 'ooo')
    l.put(40, 10, 'C').put(46, 7, 'QHQ').put(50, 11, 'S').put(50, 10, 'S').put(51, 10, 'S').put(51, 11, 'S')
    l.put(58, 7, '===').put(84, 7, '===').put(71, 10, 'K').put(92, 10, 'p')
    return l


STAGES = [stage_1_1, stage_1_2, stage_2_1, stage_2_2, stage_3_1, stage_3_2, stage_4_1, stage_4_2]


def main():
    out = ['/* Generated by prepare_levels.py; do not edit. */']
    entries = []
    for number, build in enumerate(STAGES):
        level = build()
        rows = level.rows()
        text = ''.join(rows)
        assert len(rows) == ROWS and all(len(row) == level.width for row in rows)
        assert text.count('@') == 1, level.name
        assert text.count('P') + text.count('p') == 1, level.name
        assert sum(text.count(c) for c in 'eahxK') <= 24, level.name
        assert sum(text.count(c) for c in 'MV') <= 8, level.name
        assert text.count('C') <= 1 and len(level.name) <= 15
        runs = level.runs()
        out.append('static const run_t stage%d_runs[]={' % number)
        for start in range(0, len(runs), 8):
            out.append('    ' + ''.join("{%d,%d,%d,'%s'}," % run for run in runs[start:start + 8]))
        out.append('};')
        entries.append('    {stage%d_runs,%d,%d,%d,"%s"},' % (number, len(runs), level.width, level.theme, level.name))
    out.append('static const stage_t stages[STAGE_COUNT]={')
    out.extend(entries)
    out.append('};')
    (ROOT / 'levels_generated.h').write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')
    print('levels_generated.h', len(STAGES), 'stages')


if __name__ == '__main__':
    main()
