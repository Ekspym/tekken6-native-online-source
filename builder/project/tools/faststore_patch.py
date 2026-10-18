# Cheaper guest stores and loads (2026-10-11, cost of the live and the re-simulated frame). Run last, after
# pagemap_patch.py, rawstore_patch.py and sndcall_patch.py:  python -I tools/faststore_patch.py generated/default
#  - stores through the stack pointer (ctx.r1 + constant: register saves of every prologue, locals, parameters; a third
#    of all store sites) become T6_STORE_SP_*: when the address is a thread stack (0x7xxxxxxx - never saved, restored
#    or recorded) the store skips the page marks, the fine map and the sound capture; any other r1 takes the full path
#  - the value watches (t6_watch_on at every store, t6_rwatch_on at every load) are compiled in only with
#    -DT6_WATCH_BUILD (they are diagnostics; the flag test alone ran at every guest memory access)
import glob, re, sys

d = sys.argv[1]
MARK = '// T6 stack stores (tools/faststore_patch.py)'
PCH_BLOCK = '''
// ---- T6 cheap stores / loads (tools/faststore_patch.py) ----
#ifndef T6_WATCH_BUILD
#undef T6_STORE_WATCH
#define T6_STORE_WATCH(x, y, n) ((void)0)
#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#define REX_LOAD_U8(x) (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#endif
// A store through the stack pointer: thread stacks (0x70000000-0x7FFFFFFF) are outside every snapshot, map and record
#define T6_SP_NOTE(x, y, n) ((((u32)(x)) >> 28) == 7 ? (void)0 : (void)(T6_STORE_MARK(x), T6_STORE_WATCH(x, y, n), T6_CAP((x), (n))))
#define T6_STORE_SP_U8(x, y)  (T6_SP_NOTE(x, y, 1), *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define T6_STORE_SP_U16(x, y) (T6_SP_NOTE(x, y, 2), *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define T6_STORE_SP_U32(x, y) (T6_SP_NOTE(x, y, 4), *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define T6_STORE_SP_U64(x, y) (T6_SP_NOTE(x, y, 8), *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
'''

direct = re.compile(r'\bREX_STORE_U(8|16|32|64)\(ctx\.r1\.u32 \+ ')
# stwu r1,-n(r1):  ea = -n + ctx.r1.u32;  REX_STORE_U32(ea, ctx.r1.u32);
update = re.compile(r'(ea = -?\d+ \+ ctx\.r1\.u32;\r?\n\s*)REX_STORE_U(32|64)\(ea, ')
n = 0
for p in sorted(glob.glob(d + '/tekken6_recomp.*.cpp')):
    s = open(p, encoding='utf-8', newline='').read()
    if MARK in s:
        continue
    s, a = direct.subn(r'T6_STORE_SP_U\1(ctx.r1.u32 + ', s)
    s, b = update.subn(r'\1T6_STORE_SP_U\2(ea, ', s)
    n += a + b
    open(p, 'w', encoding='utf-8', newline='').write(MARK + '\n' + s)
print(f'{d}: {n} stack stores')

p = d + '/tekken6_pch.h'
s = open(p, encoding='utf-8', newline='').read()
if 'tools/faststore_patch.py' not in s:
    # appended at the end: it has to come after the sound boundary's T6_STORE_WATCH / T6_CAP and the load watch
    # (where the other patch tools put their blocks depends on the order they ran in)
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.rstrip('\r\n') + nl + PCH_BLOCK.replace('\n', nl)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('pch patched')
