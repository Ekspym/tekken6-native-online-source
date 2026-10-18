#!/usr/bin/env python3
"""Shader / pipeline description storage of the game (ReXGlue D3D12 pipeline cache): count and merge.

The game keeps, per run folder, cache\\shaders\\shareable\\4E4D07FC.xsh (the guest shaders it met) and
4E4D07FC.rov.d3d12.xpso (the pipeline descriptions it met). Both hold descriptions, not compiled code, so they move
between PCs; at its start the game creates every stored pipeline, and a pipeline it only meets in a match costs a
hitch there. The package ships a merged storage (tools\\shader_cache\\) that the launcher adds to the player's.

  shader_cache.py count <folder or file>...          records per storage
  shader_cache.py merge <out folder> <folder>...      union of the storages (first source first), checked
"""
import os
import pathlib
import struct
import sys

XSH = '4E4D07FC.xsh'
XPSO = '4E4D07FC.rov.d3d12.xpso'
XSH_MAGIC = 0x48534558  # 'XESH'
XSH_VERSION = 0x20201219
XPSO_MAGIC = 0x53504558  # 'XEPS'
XPSO_API = 0x4F525844  # 'DXRO'
XPSO_VERSION = 0x20260226  # max(PipelineDescription::kVersion, DxbcShaderTranslator::Modification::kVersion)
PIPELINE = 72  # u64 hash + PipelineDescription


def swap32(v):
    return struct.unpack('<I', struct.pack('>I', v))[0]


try:
    import xxhash

    def xxh3(b):
        return xxhash.xxh3_64_intdigest(b)
except ImportError:  # the records are then taken as they are
    xxh3 = None


def read_xsh(path):
    """-> list of (hash, record bytes) in file order, without torn / wrong records."""
    out = []
    try:
        d = pathlib.Path(path).read_bytes()
    except OSError:
        return out
    if len(d) < 8 or struct.unpack_from('<II', d, 0) != (XSH_MAGIC, swap32(XSH_VERSION)):
        return out
    p = 8
    while p + 12 <= len(d):
        h, w = struct.unpack_from('<QI', d, p)
        n = (w & 0x7FFFFFFF) * 4
        if p + 12 + n > len(d):
            break
        if xxh3 and xxh3(d[p + 12:p + 12 + n]) != h:
            break
        out.append((h, d[p:p + 12 + n]))
        p += 12 + n
    return out


def read_xpso(path):
    out = []
    try:
        d = pathlib.Path(path).read_bytes()
    except OSError:
        return out
    if len(d) < 12 or struct.unpack_from('<III', d, 0) != (XPSO_MAGIC, XPSO_API, swap32(XPSO_VERSION)):
        return out
    for p in range(12, len(d) - PIPELINE + 1, PIPELINE):
        h, = struct.unpack_from('<Q', d, p)
        if xxh3 and xxh3(d[p + 8:p + PIPELINE]) != h:
            break
        out.append((h, d[p:p + PIPELINE]))
    return out


def find(path):
    """A storage folder under path (a run folder, a cache folder, the shareable folder itself)."""
    for sub in ('', 'shareable', os.path.join('shaders', 'shareable'), os.path.join('cache', 'shaders', 'shareable')):
        f = os.path.join(path, sub)
        if os.path.exists(os.path.join(f, XSH)):
            return f
    return None


def union(lists):
    seen, out = set(), []
    for l in lists:
        for h, rec in l:
            if rec not in seen:
                seen.add(rec)
                out.append((h, rec))
    return out


def merge(out, sources):
    shaders = union(read_xsh(os.path.join(s, XSH)) for s in sources)
    pipes = union(read_xpso(os.path.join(s, XPSO)) for s in sources)
    # a pipeline whose shaders are not stored cannot be created at the start - leave it out
    have = {h for h, _ in shaders}
    kept = []
    for h, rec in pipes:
        vs, _, ps = struct.unpack_from('<QQQ', rec, 8)
        if vs in have and (ps == 0 or ps in have):
            kept.append((h, rec))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, XSH), 'wb') as f:
        f.write(struct.pack('<II', XSH_MAGIC, swap32(XSH_VERSION)))
        for _, rec in shaders:
            f.write(rec)
    with open(os.path.join(out, XPSO), 'wb') as f:
        f.write(struct.pack('<III', XPSO_MAGIC, XPSO_API, swap32(XPSO_VERSION)))
        for _, rec in kept:
            f.write(rec)
    return len(shaders), len(kept), len(pipes) - len(kept)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('count', 'merge'):
        print(__doc__)
        return 2
    if sys.argv[1] == 'count':
        for a in sys.argv[2:]:
            f = find(a)
            if not f:
                print('%s: no storage' % a)
                continue
            print('%s: %d shaders, %d pipelines' % (a, len(read_xsh(os.path.join(f, XSH))), len(read_xpso(os.path.join(f, XPSO)))))
        return 0
    sources = [f for f in (find(a) for a in sys.argv[3:]) if f]
    s, p, dropped = merge(sys.argv[2], sources)
    print('%d storages -> %d shaders, %d pipelines (%d without their shaders left out)%s' % (
        len(sources), s, p, dropped, '' if xxh3 else '; xxhash module missing - records not checked'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
