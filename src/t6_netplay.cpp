// Tekken 6 netplay (online plan in NOTES.md: offline Versus + GGPO, like SF4 Ember Netplay).
//
// Input point: midasm hook T6InputHook at 0x82268A0C (tekken6_config.toml). The entry builder sub_822688E0 (PS3
// FUN_00186a10) has just built both players' 0x4C input entries on its stack (P1 at r1+80, P2 at r1+156) and is about
// to hand them to the input manager (sub_823FAA08). It runs once per frame in the pad step (sub_82268A60), before
// menus (pad objects) and the fight (input manager entries) read input.
//
// Tools:
//   T6_REC=<file>        record every step: u32 step + 2 x 0x4C entries + pad block kPadLo..kPadHi
//   T6_PLAY=<file>       replay such a file (entries + pad block overwritten, controllers ignored until it ends)
//   T6_HASH=<file>       per step: step + FNV-1a 64 per range (T6_HASH_RANGES=lo-hi,..., default: XEX data/bss)
//   T6_FIX_SEED=<n>      replace the seeds of srand (main LCG) and the CRT srand(time(NULL)) at boot
//   T6_SNAPTEST=<s>,<k>  at step s save guest memory, run k steps (recording inputs + hashes), restore, run the same
//                        k inputs again and compare the hashes (log lines "snaptest")
//
// The netplay code is split by area: t6_netplay_internal.h (shared declarations) and the t6_*.cpp modules
// listed in CMakeLists.txt. This file keeps the globals the generated code and the runtime see.

#include "t6_netplay_internal.h"

double g_audio_parts[3] = {};
double g_rbp_load0 = 0;  // audio step: XAudio callbacks (worker), XAudio thread idle, CnuSound tick
uint8_t* g_t6_membase = nullptr;  // guest memory base, set by Tekken6App after the XEX is loaded
bool (*g_t6_gpu_idle)() = nullptr;
uint64_t (*g_t6_gpu_ptrs)() = nullptr;  // set by Tekken6App: GPU ring read index << 32 | write index (diagnostics)  // set by Tekken6App: the GPU executed everything submitted
uint32_t (*g_t6_sys_alloc)(uint32_t, uint32_t) = nullptr;  // set by Tekken6App: ReXGlue system heap (sound library)
void (*g_t6_sys_free)(uint32_t) = nullptr;
PPCContext* g_main_ctx = nullptr;  // PPC context of the main (game) thread, set at the first frame boundary
// The main thread's CRT per-thread data (+20 = its rand() state); 0 before the first frame boundary
uint32_t T6MainPtd() {
  if (!g_main_ctx) return 0;
  PPCContext tmp = *g_main_ctx;
  __imp__sub_826A5ED0(tmp, g_t6_membase);
  return tmp.r3.u32;
}
// Page write map of the diagnostic build (tools/pagemap_patch.py): kind bits per 4 KB guest page, 1 game, 2 MMC sound
// system, 4 CnuSound2, 8 XAudio2. T6_PAGEMAP=<file>, T6_PAGEMAP_STEPS=<clear step>,<dump step>.
extern "C" unsigned char t6_pagemap[0x100000] = {};
extern "C" unsigned char t6_dirty[0x100000] = {};  // per 4 KB page: kinds that wrote it since the last netplay save (t6_mark; non-zero = dirty)
extern "C" unsigned char t6_dirty_l1[0x4000] = {};
extern "C" unsigned char t6_fine_changed = 1;
extern "C" unsigned char t6_fine_page[0x100000] = {};  // per 64 pages: one of them marked (the save / load scan 16 KB, not 1 MB)
// Marks a page written outside the generated code
extern "C" void t6_dirty_mark(uint32_t a) {
  t6_dirty[a >> 12] = 1;
  t6_dirty_l1[a >> 18] = 1;
}
extern "C" unsigned char t6_fine_heap[0x480000] = {};  // 16-byte blocks of 0xA7290000-0xABA90000
extern "C" unsigned char t6_fine_data[0x69000] = {};   // 16-byte blocks of 0x82726000-0x82DB8000
extern "C" unsigned char t6_fine_virt[0x400000] = {};  // 16-byte blocks of 0x40000000-0x44000000
// 32 on every thread except the main one (T6InputHook runs on it and clears it): foreign writes, as in PS3 rb_state
extern "C" constinit thread_local unsigned char t6_thread_bit = 32;  // set by Tekken6App: the GPU executed everything submitted
