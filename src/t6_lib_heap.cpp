// Tekken 6 netplay - own heap of the sound library (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Own heap of the sound library (PS3: "the library allocates in the game heap H -> it needs its own heap") ----
// XMemAlloc / XMemFree (sub_824C64B8 / sub_824C6550) called by CnuSound2 / XAudio2 code (tools/sndcall_patch.py)
// come here: virtual blocks go to the ReXGlue system heap, outside the game heaps, and stay live across a load.
// Physical blocks (attributes bit 31: XMA buffers for the audio hardware) keep using the original functions.
extern "C" void __imp__sub_824C64B8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_824C6550(PPCContext& ctx, uint8_t* base);
namespace t6np {
std::mutex g_lib_mutex;
std::map<uint32_t, uint32_t> g_lib_blocks;  // address -> size
}  // namespace t6np

extern "C" void t6_lib_alloc(PPCContext& ctx, uint8_t* base) {
  const uint32_t size = ctx.r3.u32, attr = ctx.r4.u32;
  if ((attr & 0x80000000u) || !g_t6_sys_alloc || !T6Env("T6_SOUND_SEPARATE")) {
    __imp__sub_824C64B8(ctx, base);
    return;
  }
  const uint32_t addr = g_t6_sys_alloc(size ? size : 1, 128);
  {
    std::lock_guard lock(g_lib_mutex);
    static uint32_t failed = 0;
    if (!addr) {
      if (failed++ < 20) REXLOG_WARN("T6 sound: library heap allocation of {} bytes failed ({} blocks live)", size, g_lib_blocks.size());
    } else {
      g_lib_blocks[addr] = size;
    }
  }
  ctx.r3.u64 = addr;
}

extern "C" void t6_lib_free(PPCContext& ctx, uint8_t* base) {
  const uint32_t addr = ctx.r3.u32;
  {
    std::lock_guard lock(g_lib_mutex);
    if (auto it = g_lib_blocks.find(addr); it != g_lib_blocks.end()) {
      g_lib_blocks.erase(it);
      // Deferred: a restored game / MMC object may still point here (PS3: freed library memory stayed mapped)
      static std::deque<std::pair<uint32_t, uint32_t>> pending;  // (address, step of the free)
      const uint32_t now = state().step;
      pending.emplace_back(addr, now);
      while (!pending.empty() && pending.front().second + 600 < now) {
        if (g_t6_sys_free) g_t6_sys_free(pending.front().first);
        pending.pop_front();
      }
      return;
    }
  }
  __imp__sub_824C6550(ctx, base);
}

namespace t6np {
bool T6InLibHeap(uint32_t a) {
  std::lock_guard lock(g_lib_mutex);
  auto it = g_lib_blocks.upper_bound(a);
  if (it == g_lib_blocks.begin()) return false;
  --it;
  return a < it->first + it->second;
}
// Blocks of the library heap (kept live by a load)
std::vector<Range> T6LibBlocks() {
  std::lock_guard lock(g_lib_mutex);
  std::vector<Range> v;
  v.reserve(g_lib_blocks.size());
  for (const auto& [a, n] : g_lib_blocks) v.push_back({a, a + ((n + 15) & ~15u)});
  return v;
}
}  // namespace t6np
