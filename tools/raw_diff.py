# Compares runs\<A>\raw_<frame>.bin and runs\<B>\raw_<frame>.bin (T6_NET_RAWDUMP): the 32-bit words (guest byte order)
# that differ between two instances. With a third argument (the previous frame of A and B: "prevA,prevB") only words
# that were still equal one frame earlier are listed = what went apart in that frame.
import pathlib, struct, sys


def load(p):
    d = pathlib.Path(p).read_bytes()
    out = {}
    for o in range(0, len(d), 4100):
        out[struct.unpack_from('<I', d, o)[0]] = d[o + 4:o + 4100]
    return out


def words(a, b):
    out = {}
    for page in sorted(set(a) & set(b)):
        x, y = a[page], b[page]
        if x == y:
            continue
        for o in range(0, 4096, 4):
            if x[o:o + 4] != y[o:o + 4]:
                out[page + o] = (x[o:o + 4].hex(), y[o:o + 4].hex())
    return out


cur = words(load(sys.argv[1]), load(sys.argv[2]))
old = {}
if len(sys.argv) > 3:
    pa, pb = sys.argv[3].split(',')
    old = words(load(pa), load(pb))
new = {k: v for k, v in cur.items() if k not in old}
print(f'{len(cur)} words differ, {len(new)} new')
limit = int(sys.argv[4]) if len(sys.argv) > 4 else 200
for k in sorted(new)[:limit]:
    print(f'{k:08X}: {new[k][0]} | {new[k][1]}')
