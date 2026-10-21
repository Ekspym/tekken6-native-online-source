# Diagnostic build (like fix_pagemap.py of the PS3 hybrid): every generated function gets its code kind, every guest
# store marks the written 4 KB page with that kind in t6_pagemap (src/t6_netplay.cpp writes it out, T6_PAGEMAP=<file>).
# Kinds: 1 game, 2 MMC sound system, 4 CnuSound2, 8 XAudio2, 16 Direct3D, 128 worker thread infrastructure; 32 = written by a thread other than
# the main thread (PS3 rb_state: "foreign" pages). Run after codegen; undo with `git checkout generated`
# is not possible (generated/ is ignored) -> re-run codegen (delete generated/default/codegen.build.stamp).
#   python -I tools/pagemap_patch.py generated/default
import glob, pathlib, re, sys

KINDS = [  # (lo, hi, kind)
    (0x82338000, 0x82358000, 2),  # MMC sound system (MMC_* classes, tcb_device_sound_t)
    (0x82570000, 0x82590000, 4),  # CnuSound2 (CnuSound2FxComp, XAudio2 voices)
    (0x824EE000, 0x82503000, 8),  # XAudio2 (statically linked)
    (0x824D0000, 0x824EE000, 16),  # Direct3D (callers of the Vd* video driver imports 0x824D5060-0x824EAD28)
    (0x82516000, 0x82529000, 128),  # worker thread infrastructure: request queues, worker loop (82524FF8), requests
]

PCH_BLOCK = '''
// ---- T6 pagemap diagnostic (tools/pagemap_patch.py) ----
extern "C" unsigned char t6_pagemap[0x100000];
#define T6_KIND(k) [[maybe_unused]] constexpr unsigned char t6_kind = (k)
static constexpr unsigned char t6_kind = 1;  // code outside generated functions
extern "C" constinit thread_local unsigned char t6_thread_bit;  // 0 on the main (game) thread, 32 on every other thread
// Fine map (16-byte blocks) of the game heaps and the XEX data/bss: the sound code shares pages with game objects
// (PS3 fix_pagemap2), so the sound bytes are excluded at 16-byte granularity
extern "C" unsigned char t6_fine_heap[0x480000];  // 0xA7290000-0xABA90000
extern "C" unsigned char t6_fine_data[0x69000];   // 0x82726000-0x82DB8000
extern "C" unsigned char t6_fine_virt[0x400000];  // 0x40000000-0x44000000
extern "C" unsigned char t6_dirty[0x100000];  // per 4 KB page: written since the last netplay save
extern "C" unsigned char t6_dirty_l1[0x4000];
extern "C" unsigned char t6_fine_page[0x100000];  // per page: its fine-map bytes got a new kind
extern "C" unsigned char t6_fine_changed;  // a block of the fine maps got a new kind since the last calibration  // per 64 pages: some page of them marked in t6_dirty (scans skip the rest)
// Runs at EVERY guest store, on every thread (2026-10-11: cost of the live frame). The two page tables are only
// written when the store changes them (they were written at every store: the main thread and the draw worker kept
// stealing each other's cache lines), and the 16-byte map is only kept for the code kinds its users ask for - the
// libraries, Direct3D and the worker infrastructure (masks 0x90 / 0x9C / 0x0C in the calibration and the sound
// boundary); the kind is a constant of the function, so game code (kinds 1, 2 = 22700 of 25000 functions) has no fine
// map code at all.
// Megabytes that hold anything a save, a load or the maps' readers look at: 0x40000000-0x4FFFFFFF (virtual buffers,
// D3D device), 0x82700000-0x82DFFFFF (XEX data / bss), 0xA7200000-0xABAFFFFF (game heaps). Stores of the libraries,
// Direct3D and the worker code to any other place (the sound library's own heap in the system heap: its mixing loops
// store every sample; the GPU pools and command buffers from 0xABB00000 on) leave no mark at all.
struct T6TopTable {
  unsigned char v[4096];
  constexpr T6TopTable() : v{} {
    for (unsigned t = 0; t < 4096; t++) v[t] = (t - 0x400u < 0x100u) || (t - 0x827u < 7u) || (t - 0xA72u < 0x49u);
  }
};
inline constexpr T6TopTable t6_track_top{};
static inline void t6_mark(unsigned char k, u32 x) {
  if ((k & 0x9C) && !t6_track_top.v[x >> 20]) return;
  // One test per store: the dirty byte holds the kinds that wrote the page since the last save (non-zero = dirty),
  // the kind map is only looked at when the dirty byte gets a new kind.
  unsigned char* const d = &t6_dirty[x >> 12];
  if ((*d & k) != k) {
    *d |= k;
    t6_dirty_l1[x >> 18] = 1;
    unsigned char* const m = &t6_pagemap[x >> 12];
    if ((*m & k) != k) *m |= k;
  }
  if (k & 0x9C) {
    unsigned char* f = nullptr;
    if (x - 0xA7290000u < 0x4800000u) f = &t6_fine_heap[(x - 0xA7290000u) >> 4];
    else if (x - 0x82726000u < 0x692000u) f = &t6_fine_data[(x - 0x82726000u) >> 4];
    else if (x - 0x40000000u < 0x4000000u) f = &t6_fine_virt[(x - 0x40000000u) >> 4];
    // (a new byte kind for the 16-byte block: the save's exclusion list has to be built again)
    if (f && (*f & k) != k) { *f |= k; t6_fine_changed = 1; t6_fine_page[x >> 12] = 1; }
  }
}
// 0x40 = written by the main thread, 0x20 = by another thread (PS3 rb_state foreign writes)
#define T6_MARK(x) t6_mark((unsigned char)(t6_kind | (t6_thread_bit ? 0x20 : 0x40)), (u32)(x))
// Store value watch (T6_WATCH, src/t6_netplay.cpp t6_watch): logs stores of chosen values with the function name
extern "C" int t6_watch_on;
extern "C" void t6_watch(u32 addr, unsigned long long value, int size, const char* fn);
#define T6_WATCH(x, y, n) (t6_watch_on ? t6_watch((u32)(x), (unsigned long long)(y), (n), __func__) : (void)0)
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_STORE_U8(x, y)  (T6_MARK(x), T6_WATCH(x, y, 1), *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (T6_MARK(x), T6_WATCH(x, y, 2), *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (T6_MARK(x), T6_WATCH(x, y, 4), *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (T6_MARK(x), T6_WATCH(x, y, 8), *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
'''


def kind(addr):
    for lo, hi, k in KINDS:
        if lo <= addr < hi:
            return k
    return 1


# Virtual time base of the main thread (PS3 RB_VIRTUAL_TB, src/t6_netplay.cpp t6_timebase)
TB_BLOCK = '''
// Load value watch (T6_RWATCH, src/t6_watch.cpp): the loaded value passes through t6_rwatch while the watch is on
extern "C" int t6_rwatch_on;
extern "C" unsigned t6_rwatch(u32 addr, unsigned value, int size, const char* fn);
#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#define REX_LOAD_U8(x) (t6_rwatch_on ? (u8)t6_rwatch((u32)(x), (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))), 1, __func__) : (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))))
#define REX_LOAD_U16(x) (t6_rwatch_on ? (u16)t6_rwatch((u32)(x), __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))), 2, __func__) : __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))))
#define REX_LOAD_U32(x) (t6_rwatch_on ? (u32)t6_rwatch((u32)(x), __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))), 4, __func__) : __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))))
// ---- T6 virtual timebase (tools/pagemap_patch.py) ----
extern "C" uint64_t t6_timebase();
#undef REX_QUERY_TIMEBASE
#define REX_QUERY_TIMEBASE() t6_timebase()
'''

d = sys.argv[1]
pch = d + '/tekken6_pch.h'
s = pathlib.Path(pch).read_text(encoding='utf-8')
if 'T6 pagemap diagnostic' not in s:
    s += PCH_BLOCK
if 'T6 virtual timebase' not in s:
    s += TB_BLOCK
pathlib.Path(pch).write_text(s, encoding='utf-8')
n = 0
pat = re.compile(r'(DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{\n\tREX_FUNC_PROLOGUE\(\);)')
for p in sorted(glob.glob(d + '/tekken6_recomp.*.cpp')):
    s = pathlib.Path(p).read_text(encoding='utf-8')
    if 'T6_KIND(' in s:
        continue
    s, c = pat.subn(lambda m: m.group(1) + '\n\tT6_KIND(%d);' % kind(int(m.group(2), 16)), s)
    n += c
    pathlib.Path(p).write_text(s, encoding='utf-8')
print(f'{d}: kind set in {n} functions')
