# Compares runs\<A>\fight_state.csv and runs\<B>\fight_state.csv (confirmed frames; the last row of a frame counts):
# columns frame, 4 position floats, main LCG (stage particles = cosmetic), data/bss checksum, players hash (the two
# fight player objects), third RNG state, player object addresses. Desync criterion = players hash + third RNG.
import sys


def load(p):
    rows = {}
    with open(p) as fh:
        for l in fh:
            c = l.strip().split(',')
            if len(c) >= 9 and c[0].isdigit():
                rows[int(c[0])] = c
    return rows


a, b = load(sys.argv[1]), load(sys.argv[2])
frames = sorted(set(a) & set(b))
names = {5: 'LCG(cosmetic)', 6: 'area', 7: 'players', 8: 'rng3'}
print(f'{len(frames)} common frames ({frames[0] if frames else "-"}..{frames[-1] if frames else "-"})')
for col, name in names.items():
    d = [f for f in frames if a[f][col] != b[f][col]]
    print(f'  {name}: {len(d)} differ' + (f', first {d[0]}, last {d[-1]}' if d else ''))
pos = [f for f in frames if a[f][1:5] != b[f][1:5]]
print(f'  positions: {len(pos)} differ' + (f', first {pos[0]}' if pos else ''))
