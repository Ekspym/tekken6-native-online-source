# Searches a raw page dump (T6_NET_RAWDUMP: u32 page address + 4096 bytes per page) for a string (ASCII and UTF-16 BE)
# or for a big-endian 32-bit value (0x...): prints the guest addresses and a hex/ASCII context.
# Usage: raw_find.py <raw_N.bin> <text | 0xVALUE> [context bytes]
import struct, sys

def load(path):
    pages = {}
    with open(path, 'rb') as f:
        while True:
            h = f.read(4)
            if len(h) < 4:
                break
            pages[struct.unpack('<I', h)[0]] = f.read(4096)
    return pages

def runs(pages):
    # contiguous pages joined, so that matches across a page end are found
    out, cur, start, prev = [], [], None, None
    for a in sorted(pages):
        if prev is not None and a != prev + 0x1000:
            out.append((start, b''.join(cur)))
            cur, start = [], None
        if start is None:
            start = a
        cur.append(pages[a])
        prev = a
    if cur:
        out.append((start, b''.join(cur)))
    return out

def show(base, data, off, ctx):
    lo = max(0, (off - ctx) & ~15)
    hi = min(len(data), off + ctx)
    for o in range(lo, hi, 16):
        row = data[o:o + 16]
        print('  %08X  %-47s  %s' % (base + o, row.hex(' '), ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)))

def main():
    pages = load(sys.argv[1])
    what = sys.argv[2]
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 48
    if what.startswith('0x'):
        pats = [('value', struct.pack('>I', int(what, 16)))]
    else:
        pats = [('ascii', what.encode('ascii')), ('utf16be', what.encode('utf-16-be'))]
    for kind, pat in pats:
        for base, data in runs(pages):
            o = data.find(pat)
            while o >= 0:
                print('%s at %08X' % (kind, base + o))
                if ctx:
                    show(base, data, o, ctx)
                o = data.find(pat, o + 1)

main()
