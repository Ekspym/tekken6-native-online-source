# Contact sheets of the screenshots of tools\e2e_shots.ps1: shots_sheet.py <dir> <out prefix> -> <prefix>_N.png (48 per sheet)
import sys, glob, os
from PIL import Image, ImageDraw
files = sorted(glob.glob(os.path.join(sys.argv[1], '*.png')))
W, H, COLS, PER = 320, 180, 6, 48
for k in range(0, len(files), PER):
    part = files[k:k + PER]
    rows = (len(part) + COLS - 1) // COLS
    sheet = Image.new('RGB', (COLS * W, rows * (H + 14)), 'black')
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(part):
        x, y = (i % COLS) * W, (i // COLS) * (H + 14)
        sheet.paste(Image.open(f).convert('RGB').resize((W, H)), (x, y + 14))
        d.text((x + 2, y + 1), os.path.basename(f)[:-4], fill='yellow')
    out = '%s_%d.png' % (sys.argv[2], k // PER)
    sheet.save(out)
    print(out, len(part))
