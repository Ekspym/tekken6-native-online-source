# Marks the guest stores the code generator writes without REX_STORE_*: vector stores (stvx & co,
# simde_mm_store_si128 to REX_RAW_ADDR), dcbz / dcbzl (memset of 32 / 128 bytes) and the atomic stores of stwcx / stdcx
# (__sync_bool_compare_and_swap). Without this the page map (pagemap_patch.py) and the per-save dirty map missed
# every page the game only writes with VMX or dcbz (11029 + 95 + ~230 sites). Run after pagemap_patch.py:
#   python -I tools/rawstore_patch.py generated/default
import glob, pathlib, re, sys

d = sys.argv[1]
pats = [
    re.compile(r'^(\s+)(simde_mm_store_si128\(\(simde__m128i\*\)REX_RAW_ADDR\(([^()]*(?:\([^()]*\))?[^()]*)\).*)$'),
    re.compile(r'^(\s+)(memset\(\(void\*\)REX_RAW_ADDR\(([^()]*(?:\([^()]*\))?[^()]*)\).*)$'),
    re.compile(r'^(\s+)(ctx\.cr0\.eq = __sync_bool_compare_and_swap\(reinterpret_cast<uint(?:32|64)_t\*>\(REX_RAW_ADDR\(([^()]*(?:\([^()]*\))?[^()]*)\)\).*)$'),
]
n = 0
for p in sorted(glob.glob(d + '/tekken6_recomp.*.cpp')):
    s = pathlib.Path(p).read_text(encoding='utf-8')
    if 'T6 raw stores marked' in s:
        continue
    out = []
    for line in s.split('\n'):
        for pat in pats:
            m = pat.match(line)
            if m:
                line = f'{m.group(1)}T6_MARK({m.group(3)}); {m.group(2)}'
                n += 1
                break
        out.append(line)
    pathlib.Path(p).write_text('// T6 raw stores marked (tools/rawstore_patch.py)\n' + '\n'.join(out), encoding='utf-8')
print(f'{d}: {n} raw stores marked')
