# Diff of two T6_SNAPDUMP files (same frame of the snapshot test's two passes): differing byte runs (merged at 64 B)
#   python -I tools/snapdump_diff.py runs/x/d1.bin runs/x/d2.bin
import pathlib, struct, sys

def load(p):
    d = {}
    b = pathlib.Path(p).read_bytes()
    o = 0
    while o < len(b):
        a, n = struct.unpack_from('<II', b, o)
        d[a] = b[o + 8:o + 8 + n]
        o += 8 + n
    return d

A, B = load(sys.argv[1]), load(sys.argv[2])
runs = []
for a in sorted(A):
    x, y = A[a], B.get(a)
    if y is None or len(y) != len(x):
        continue
    i = 0
    while i < len(x):
        if x[i] != y[i]:
            j = i
            while j < len(x) and (x[j] != y[j] or x[j:j + 64] != y[j:j + 64]):
                j += 1
            runs.append((a + i, j - i, x[i:min(j, i + 16)].hex(), y[i:min(j, i + 16)].hex()))
            i = j
        else:
            i += 1
print(len(runs), 'runs,', sum(r[1] for r in runs), 'bytes')
for a, n, x, y in runs[:200]:
    print(f'{a:08X} {n:6d}  {x}  {y}')
