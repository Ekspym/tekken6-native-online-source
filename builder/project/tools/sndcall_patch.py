# Sound boundary (port of the PS3 hybrid's fix_sndcall.py / RB_SOUND_SEPARATE). Run after codegen (and after
# pagemap_patch.py if that diagnostic is used):  python -I tools/sndcall_patch.py generated/default
#  - every generated function gets `t6_capfn`: 1 in the sound library and the helper code it uses, 0 elsewhere
#  - guest stores of t6_capfn code are recorded while a boundary call runs (T6_CAP -> t6_cap)
#  - direct calls and indirect calls (vtables) from game code into the sound library go through t6_snd_call
#    (src/t6_netplay.cpp): live frames run and record the call (result + writes into restored memory), re-simulated
#    frames replay it, so the library - which is not rolled back - sees every action once.
import glob, re, sys

# MMC (0x82338000-0x82358000) is NOT library: its MMC_TCB_* objects are game tasks in the task tree (excluding
# their bytes broke the tree: endless recursion in sub_822190B8). Like PS3: game sound objects roll back, bnus doesn't.
LIB = [
    (0x82570000, 0x82590000),  # CnuSound2
    (0x824EE000, 0x82503000),  # XAudio2
]
CAP = LIB + [
    (0x826A0000, 0x826B8000),  # CRT (memcpy, memset, ...) used by the library
    (0x8223B000, 0x8223E000),  # game heap allocator (the library allocates in the game heaps)
]

PCH_BLOCK = '''
// ---- T6 sound boundary (tools/sndcall_patch.py) ----
extern "C" constinit thread_local int t6_snd_capture;
extern "C" void t6_cap(u32 a, u32 n);
extern "C" void t6_snd_call(PPCContext& ctx, uint8_t* base, u32 addr, PPCFunc* fn);
extern "C" void t6_lib_alloc(PPCContext& ctx, uint8_t* base);  // XMemAlloc of the library -> own heap
extern "C" void t6_lib_free(PPCContext& ctx, uint8_t* base);   // XMemFree
#define T6_CAPFN(k) [[maybe_unused]] constexpr int t6_capfn = (k)
static constexpr int t6_capfn = 0;
#define T6_CAP(x, n) ((t6_capfn && t6_snd_capture) ? t6_cap((u32)(x), (n)) : (void)0)
#define T6_IN_LIB(a) (((u32)(a) - 0x82570000u < 0x20000u) || ((u32)(a) - 0x824EE000u < 0x15000u))
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#ifdef T6_MARK
#define T6_STORE_MARK(x) T6_MARK(x)
#else
#define T6_STORE_MARK(x) ((void)0)
#endif
#ifdef T6_WATCH
#define T6_STORE_WATCH(x, y, n) T6_WATCH(x, y, n)
#else
#define T6_STORE_WATCH(x, y, n) ((void)0)
#endif
#define REX_STORE_U8(x, y)  (T6_STORE_MARK(x), T6_STORE_WATCH(x, y, 1), T6_CAP((x), 1), *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (T6_STORE_MARK(x), T6_STORE_WATCH(x, y, 2), T6_CAP((x), 2), *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (T6_STORE_MARK(x), T6_STORE_WATCH(x, y, 4), T6_CAP((x), 4), *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (T6_STORE_MARK(x), T6_STORE_WATCH(x, y, 8), T6_CAP((x), 8), *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
// Indirect call from game code: a target in the sound library goes through the boundary
#define T6_SND_INDIRECT(x)                                                              \\
  do {                                                                                  \\
    const u32 t6_target_ = (u32)(x);                                                    \\
    PPCFunc* t6_fn_ = T6_IN_LIB(t6_target_) ? REX_LOOKUP_FUNC(base, t6_target_) : nullptr; \\
    if (t6_fn_) t6_snd_call(ctx, base, t6_target_, t6_fn_);                             \\
    else REX_CALL_INDIRECT_FUNC(t6_target_);                                            \\
  } while (0)
'''


def in_ranges(a, rs):
    return any(lo <= a < hi for lo, hi in rs)


d = sys.argv[1]
pch = d + '/tekken6_pch.h'
s = open(pch, encoding='utf-8').read()
if 'T6 sound boundary' not in s:
    open(pch, 'w', encoding='utf-8').write(s + PCH_BLOCK)

FUNC = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{\n\tREX_FUNC_PROLOGUE\(\);')
CALL = re.compile(r'\b(sub_([0-9A-F]{8}))\(ctx, base\);')
nf = ndirect = nind = 0
for p in sorted(glob.glob(d + '/tekken6_recomp.*.cpp')):
    s = open(p, encoding='utf-8').read()
    if 'T6_CAPFN(' in s:
        continue
    out = []
    pos = 0
    for m in FUNC.finditer(s):
        out.append(s[pos:m.end()])
        pos = m.end()
    # split per function: rebuild with per-function processing
    parts = re.split(r'(?=DEFINE_REX_FUNC\(sub_[0-9A-F]{8}\) \{)', s)
    res = []
    for part in parts:
        m = re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{\n\tREX_FUNC_PROLOGUE\(\);', part)
        if not m:
            res.append(part)
            continue
        a = int(m.group(1), 16)
        cap = 1 if in_ranges(a, CAP) else 0
        part = part[:m.end()] + '\n\tT6_CAPFN(%d);' % cap + part[m.end():]
        nf += 1
        if in_ranges(a, LIB):
            # the library's XMemAlloc / XMemFree go to its own heap (PS3: own heap for the library)
            part = part.replace('sub_824C64B8(ctx, base);', 't6_lib_alloc(ctx, base);')
            part = part.replace('sub_824C6550(ctx, base);', 't6_lib_free(ctx, base);')
        if not in_ranges(a, LIB):
            def repl(c):
                global ndirect
                t = int(c.group(2), 16)
                if in_ranges(t, LIB):
                    ndirect += 1
                    return 't6_snd_call(ctx, base, 0x%s, %s);' % (c.group(2), c.group(1))
                return c.group(0)
            part = CALL.sub(repl, part)
            k = part.count('REX_CALL_INDIRECT_FUNC(')
            if k:
                nind += k
                part = part.replace('REX_CALL_INDIRECT_FUNC(', 'T6_SND_INDIRECT(')
        res.append(part)
    open(p, 'w', encoding='utf-8').write(''.join(res))
print(f'{d}: {nf} functions, {ndirect} direct game->sound calls, {nind} indirect call sites in game code')
