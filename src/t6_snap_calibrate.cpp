// Tekken 6 netplay - in-session calibration of the snapshot and the shadow of game memory (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

namespace t6np {
// ---- In-session calibration (PS3 RB_SNAP_CALIBRATE: the page set must come from the same session) ----
// T6_SNAP_FROM_MAP=1 (pagemap build): at the save, the snapshot ranges are the pages game code wrote since the map was
// cleared (T6_PAGEMAP_STEPS / T6_CAL_FROM), stacks and GPU pools left out; the bytes the sound library or Direct3D
// wrote keep their live value.
// Game-written pages of 0x40000000-0x50000000 seen before the write map was last cleared (netplay session start)
std::vector<uint8_t> g_pre_pages(0x10000);
// (namespace t6np continues)
void CalibrateFromMap(std::vector<Range>& ranges, std::vector<Range>& exclude, bool with_exclude) {
  ranges.clear();
  if (with_exclude) exclude.clear();
  const auto ok = [](uint32_t q) {
    // PS3: only game memory (EBOOT data + game heaps), never system memory. Here: XEX data/bss and the game heaps;
    // 0x30000000+ (ReXGlue system heap: kernel threads, library blocks) and 0x40000000+ (D3D device, system
    // allocations) stay live. Every page written since the shadow (any code kind: the bytes of the libraries keep
    // their live value through the fine exclusion), so that a page missing from the snapshot still equals the shadow.
    // T6_SNAP_ALL_PAGES=1: old rule (every page written by game code).
    static const bool all = T6Env("T6_SNAP_ALL_PAGES") != nullptr;
    // Also the game's own control data in system memory 0x40000000+ (pages game code wrote): the heap objects of the
    // game allocator (task heap *(0x828B3A10) = 0x40021B60, its blocks are in the game heaps 0xABA1xxxx; restoring the
    // blocks without it handed out a block still used by a live task, crash 82219614 with parent == new task, st55)
    // and the per-frame buffers (PS3: frame allocator 2 x 4 MB). The D3D device 0x40021D80.. stays live
    // (T6_SNAP_EXCLUDE_FILE) and so do the bytes Direct3D wrote (fine map).
    if (!all) {
      return (t6_pagemap[q] != 0 && T6InGameMemory(q)) ||
             (q >= 0x40000 && q < 0x50000 && ((t6_pagemap[q] & 0x01) || g_pre_pages[q - 0x40000]));
    }
    return (t6_pagemap[q] & 0x01) && !(q >= 0x70000 && q < 0x80000) && (q < 0xA0000 || (q >= 0xA7290 && q < 0xABA90));
  };
  // T6_SNAP_FROM_MAP=2: whole game memory (XEX data/bss + game heaps), only the exclusions come from the map (check
  // of correctness before write tracking; PS3 found tasks on pages first written after the calibration)
  if (const char* m = T6Env("T6_SNAP_FROM_MAP"); m && *m == '2') {
    ranges.push_back({0x82726AC8u, 0x82DB0000u});
    ranges.push_back({0xA7290000u, 0xABA90000u});
  } else {
  for (uint32_t pg = 0; pg < 0x100000;) {
    if (!ok(pg)) { pg++; continue; }
    uint32_t end = pg + 1;
    while (end < 0x100000 && ok(end)) end++;
    ranges.push_back({pg << 12, end << 12});
    pg = end;
  }
  }
  if (!with_exclude) return;
  const auto fine = [&](const unsigned char* m, size_t n, uint32_t base, unsigned char mask) {
    for (size_t b = 0; b < n;) {
      if (!(m[b] & mask)) { b++; continue; }
      size_t e = b + 1;
      while (e < n && (m[e] & mask)) e++;
      exclude.push_back({base + static_cast<uint32_t>(b << 4), base + static_cast<uint32_t>(e << 4)});
      b = e;
    }
  };
  // Direct3D (16) and the worker infrastructure (128) everywhere. In the XEX data/bss also CnuSound2 (4) and XAudio2
  // (8): their static state (voice lists, the XAudio locks 827D83A0 / 82984C08 and what they guard) is library memory
  // (PS3 RB_SOUND_SEPARATE); restoring it left the XAudio thread holding 827D83A0 while waiting on 82984C08 and the
  // main thread blocked on 827D83A0 (st46, st52). In the game heaps the library only writes into game objects
  // (results, replayed by the sound boundary); its own blocks are in the library heap.
  fine(t6_fine_virt, 0x400000, 0x40000000u, 0x90);
  fine(t6_fine_data, 0x69000, 0x82726000u, 0x9C);
  fine(t6_fine_heap, 0x480000, 0xA7290000u, 0x90);
}
std::vector<Range> g_cal_exclude;
bool g_cal_done = false;
// Incremental exclusions (2026-10-11: the full calibration costs ~4-5 ms and ran every 30 live frames): the fine maps
// only ever gain kinds, so after a full calibration only the pages whose fine bytes changed (t6_fine_page) are looked
// at again; their ranges are kept per page and added to the full list at a load.
// Full calibration off the main thread (PS3 RB_SNAP_CONT_CAL: calibrating continuously, not as a 4-5 ms stop of the
// main thread every 30 frames). It only reads the page / fine maps (they only ever gain marks while a session runs),
// builds new lists on a helper thread, and the next save swaps them in. T6_CAL_SYNC=1 = on the main thread as before.
struct CalJob {
  std::atomic<int> state{0};  // 0 idle, 1 running, 2 done
  std::vector<Range> ranges, exclude;
  uint32_t gen = 0, cur_gen = 0;  // a result of an older write tracking (before a session reset) is dropped
} g_cal_job;
void CalibrateAsyncStart() {
  int idle = 0;
  if (!g_cal_job.state.compare_exchange_strong(idle, 1)) return;
  g_cal_job.gen = g_cal_job.cur_gen;
  std::thread([] {
    CalibrateFromMap(g_cal_job.ranges, g_cal_job.exclude, true);
    g_cal_job.state = 2;
  }).detach();
}
void CalibrateAsyncTake() {
  if (g_cal_job.state.load() != 2) return;
  if (g_cal_job.gen != g_cal_job.cur_gen) {
    g_cal_job.state = 0;
    return;
  }
  SnapRanges().swap(g_cal_job.ranges);
  g_cal_exclude.swap(g_cal_job.exclude);
  g_cal_job.state = 0;
}
std::map<uint32_t, std::vector<Range>> g_excl_by_page;
std::vector<Range> g_excl_extra;
void RefreshExclusionsIncremental() {
  bool any = false;
  const uint64_t* w = reinterpret_cast<const uint64_t*>(t6_fine_page);
  for (uint32_t k = 0; k < sizeof(t6_fine_page) / 8; k++) {
    if (!w[k]) continue;
    for (uint32_t b = 0; b < 8; b++) {
      const uint32_t q = k * 8 + b;
      if (!t6_fine_page[q]) continue;
      t6_fine_page[q] = 0;
      const uint32_t a = q << 12;
      const unsigned char* m = nullptr;
      unsigned char mask = 0x90;
      if (a - 0xA7290000u < 0x4800000u) m = t6_fine_heap + ((a - 0xA7290000u) >> 4);
      else if (a - 0x82726000u < 0x692000u) { m = t6_fine_data + ((a - 0x82726000u) >> 4); mask = 0x9C; }
      else if (a - 0x40000000u < 0x4000000u) m = t6_fine_virt + ((a - 0x40000000u) >> 4);
      if (!m) continue;
      std::vector<Range> r;
      for (uint32_t i = 0; i < 256;) {
        if (!(m[i] & mask)) { i++; continue; }
        uint32_t e = i + 1;
        while (e < 256 && (m[e] & mask)) e++;
        r.push_back({a + (i << 4), a + (e << 4)});
        i = e;
      }
      g_excl_by_page[q] = std::move(r);
      any = true;
    }
  }
  if (!any) return;
  g_excl_extra.clear();
  for (const auto& [q, r] : g_excl_by_page) g_excl_extra.insert(g_excl_extra.end(), r.begin(), r.end());
}

// ---- Shadow of game memory (PS3 rb_state g_cal_shadow + write tracking RB_SNAP_TRACK) ----
// Taken when the page map is cleared (T6_PAGEMAP_STEPS). Every store of the pagemap build marks its page, so a game
// memory page that is unmarked at a save still has its shadow content. A page first written after the save is not in
// the snapshot; the load restores it from the shadow (the task objects of crash 82219614 were on such pages, st48 /
// st51: inserted into the task tree in pass 1, left in place by the load).
std::vector<uint8_t> g_shadow;          // 0x82726000-0x82DB8000 then 0xA7290000-0xABA90000, page aligned
std::vector<uint8_t> g_shadow_valid;    // per page: committed when the shadow was taken
constexpr uint32_t kDataLo = 0x82726000u, kDataHi = 0x82DB8000u, kHeapLo = 0xA7290000u, kHeapHi = 0xABA90000u;
constexpr uint32_t kDataPages = (kDataHi - kDataLo) >> 12, kHeapPages = (kHeapHi - kHeapLo) >> 12;
// (end of an inner block of namespace t6np)
bool T6InGameMemory(uint32_t page) {
  return (page >= (kDataLo >> 12) && page < (kDataHi >> 12)) || (page >= (kHeapLo >> 12) && page < (kHeapHi >> 12));
}
// (namespace t6np continues)
// Index of a game memory page in the shadow, or -1
int64_t ShadowIndex(uint32_t page) {
  if (page >= (kDataLo >> 12) && page < (kDataHi >> 12)) return page - (kDataLo >> 12);
  if (page >= (kHeapLo >> 12) && page < (kHeapHi >> 12)) return kDataPages + page - (kHeapLo >> 12);
  return -1;
}
uint32_t ShadowPageAddr(uint32_t idx) {
  return idx < kDataPages ? kDataLo + (idx << 12) : kHeapLo + ((idx - kDataPages) << 12);
}
// Netplay session start: the write map starts again (snapshot = pages the fight writes), game pages of 0x4xxxxxxx seen
// so far stay in the snapshot (frame buffers, allocator objects), shadow of the game memory taken now
// the page / fine write maps and the dirty map start empty
void ClearWriteMaps() {
  std::memset(t6_pagemap, 0, sizeof(t6_pagemap));
  std::memset(t6_dirty, 0, sizeof(t6_dirty));  // (t6_mark: a kind in the dirty byte is taken as present in the map)
  std::memset(t6_dirty_l1, 0, sizeof(t6_dirty_l1));
  std::memset(t6_fine_heap, 0, sizeof(t6_fine_heap));
  std::memset(t6_fine_data, 0, sizeof(t6_fine_data));
  std::memset(t6_fine_virt, 0, sizeof(t6_fine_virt));
}
void ResetWriteTracking() {
  for (uint32_t q = 0x40000; q < 0x50000; q++) g_pre_pages[q - 0x40000] |= (t6_pagemap[q] & 0x01);
  ClearWriteMaps();
  g_cal_done = false;  // fine maps cleared: the next save calibrates in full
  g_cal_job.cur_gen++;
  TakeShadow();
  void IncReset();
  IncReset();
}
void TakeShadow() {
  const uint32_t n = kDataPages + kHeapPages;
  g_shadow.assign(static_cast<size_t>(n) << 12, 0);
  g_shadow_valid.assign(n, 0);
  for (const auto& [lo, hi] : {std::pair{kDataLo, kDataHi}, std::pair{kHeapLo, kHeapHi}}) {
    ForEachCommitted(lo, hi, [&](uint32_t a, uint32_t len) {
      for (uint32_t p = a & ~0xFFFu; p < a + len; p += 0x1000) {
        const int64_t i = ShadowIndex(p >> 12);
        if (i < 0) continue;
        std::memcpy(g_shadow.data() + (static_cast<size_t>(i) << 12), g_t6_membase + p, 0x1000);
        g_shadow_valid[i] = 1;
      }
    });
  }
}

}  // namespace t6np
