# Compares runs\<A>\pagehash.bin and runs\<B>\pagehash.bin (T6_NET_PAGEHASH): per frame, the pages whose hash differs
# between the two instances; pages that differ in the first dumped frame already are listed once as "always".
import pathlib, struct, sys


def load(p):
    d = pathlib.Path(p).read_bytes()
    o = 0
    out = {}
    while o + 8 <= len(d):
        fr, n = struct.unpack_from('<II', d, o)
        o += 8
        rows = {}
        for _ in range(n):
            a, h = struct.unpack_from('<IQ', d, o)
            o += 12
            rows[a] = h
        out[fr] = rows
    return out


a, b = load(sys.argv[1]), load(sys.argv[2])
frames = sorted(set(a) & set(b))
always = None
for fr in frames:
    diff = {k for k in a[fr] if k in b[fr] and a[fr][k] != b[fr][k]}
    only = len(set(a[fr]) ^ set(b[fr]))
    if always is None:
        always = diff
        print(f'frame {fr}: {len(diff)} pages differ from the start (always), {only} pages only in one')
        continue
    new = sorted(diff - always)
    if new:
        print(f'frame {fr}: new differing pages ({len(new)}):', ' '.join(f'{x:08X}' for x in new[:40]))
        always |= set(new)
