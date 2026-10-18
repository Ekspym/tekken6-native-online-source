"""Rank icons of the launcher (launcher/ranks/NN.png, NN = the server's rank index, 0 = Beginner) cut out of a picture
of the game's rank graphics (13 rows, 4 columns, 918 x 1200): the teal background of every cell is made transparent
(flood fill from the cell's border over background-like pixels) and the icon is cropped to what is left.
  python -I tools/rank_icons.py <picture> [check sheet.png]"""
import sys
from collections import deque
from PIL import Image, ImageDraw, ImageFilter

src = Image.open(sys.argv[1]).convert('RGB')
COLS = [(0, 217), (217, 447), (447, 660), (660, 895)]
ROW = 1165 / 13
cells = {}
for row in range(13):
    cells[40 - row] = (0, row)   # Tekken God ... Champion
    cells[27 - row] = (1, row)   # Savior ... Master
    cells[14 - row] = (2, row)   # Mentor ... 8th kyu
cells[1], cells[0] = (3, 0), (3, 1)  # 9th kyu, Beginner


def background(p):
    """The picture's backdrop: very dark blue-green (the icons' own blue and green parts are brighter)."""
    r, g, b = p
    return r < 30 and 14 <= g < 72 and b < 92 and b - r > 8  # (near black = the icons' own shadows)


def cut(i):
    c, r = cells[i]
    im = src.crop((COLS[c][0], round(r * ROW), COLS[c][1], round((r + 1) * ROW)))
    w, h = im.size
    px = im.load()
    seen = [[False] * w for _ in range(h)]
    q = deque()
    for x in range(w):
        q.append((x, 0)); q.append((x, h - 1))
    for y in range(h):
        q.append((0, y)); q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y][x] or not background(px[x, y]):
            continue
        seen[y][x] = True
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # what the fill left: the icon, plus thin bright lines of the backdrop and specks. Core = the thick parts of it
    # (opening), only its large pieces; the icon = the core closed over its own dark blue / green insides (the fill
    # runs through those: Fujin, Yaksa, Raksasa, Asura) plus the thin details next to the core (sword tips, horns)
    fg = Image.new('L', (w, h), 0)
    fp = fg.load()
    for y in range(h):
        for x in range(w):
            if not seen[y][x]:
                fp[x, y] = 255
    core = fg.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))
    cp = core.load()
    label = [[0] * w for _ in range(h)]
    big = Image.new('L', (w, h), 0)
    bp = big.load()
    n = 0
    for y0 in range(h):
        for x0 in range(w):
            if cp[x0, y0] and not label[y0][x0]:
                n += 1
                comp, q2 = [], deque([(x0, y0)])
                label[y0][x0] = n
                while q2:
                    x, y = q2.popleft()
                    comp.append((x, y))
                    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if 0 <= nx < w and 0 <= ny < h and cp[nx, ny] and not label[ny][nx]:
                            label[ny][nx] = n
                            q2.append((nx, ny))
                ys = [c[1] for c in comp]
                # the icon sits in the middle of its cell; the lighter bands of the backdrop run along the cell's edges
                middle = any(w * 0.25 < x < w * 0.75 and h * 0.3 < y < h * 0.7 for x, y in comp)
                if len(comp) >= 150 and max(ys) - min(ys) >= 10 and middle:
                    for x, y in comp:
                        bp[x, y] = 255
    closed = big.filter(ImageFilter.MaxFilter(17)).filter(ImageFilter.MinFilter(17))
    near = big.filter(ImageFilter.MaxFilter(9))
    alpha = Image.new('L', (w, h), 0)
    ap, clp, np_ = alpha.load(), closed.load(), near.load()
    for y in range(h):
        for x in range(w):
            if clp[x, y] or (fp[x, y] and np_[x, y]):
                ap[x, y] = 255
    alpha = alpha.filter(ImageFilter.GaussianBlur(0.8))
    out = im.convert('RGBA')
    out.putalpha(alpha)
    box = alpha.point(lambda v: 255 if v > 40 else 0).getbbox()
    return out.crop(box) if box else out


icons = [cut(i) for i in range(41)]
for i, im in enumerate(icons):
    im.save('launcher/ranks/%02d.png' % i)
if len(sys.argv) > 2:
    sheet = Image.new('RGB', (7 * 232, 6 * 104), (25, 25, 39))
    d = ImageDraw.Draw(sheet)
    for i, im in enumerate(icons):
        sheet.paste(im, ((i % 7) * 232 + 6, (i // 7) * 104 + 4), im)
        d.text(((i % 7) * 232 + 6, (i // 7) * 104 + 92), '%d  %dx%d' % (i, im.width, im.height), fill=(140, 141, 160))
    sheet.save(sys.argv[2])
print('ok')
