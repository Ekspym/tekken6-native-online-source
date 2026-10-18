// Tekken 6 netplay - incremental snapshots: page store and undo logs (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

namespace t6np {
// ---- Incremental netplay snapshots (PS3 RB_SNAP_TRACK + page store: copy only what changed) ----
// Every guest store marks its page in t6_dirty (t6_mark; vector / dcbz / atomic stores too, tools/rawstore_patch.py).
// The image of the last save is the shadow (game memory; 0x4xxxxxxx game pages in g_inc_extra). A save of frame f
// copies only the pages written since the save of f-1: their old image (state at f-1) goes into the undo log of f,
// the image takes the new content. A load to frame t puts back the pages written since the last save from the image,
// then walks the undo logs back to t. Excluded bytes and live kernel objects keep their live value as before.
// T6_SNAP_INC=0: old full copy of the calibrated pages.
// Small fixed pool for the page copies of a save (3 helpers + the caller); fn(i) for i in [0, n), chunks of 32
void T6ParallelFor(size_t n, void (*fn)(size_t)) {
  struct Pool {
    std::mutex m;
    std::condition_variable cv, done_cv;
    void (*fn)(size_t) = nullptr;
    size_t n = 0;
    std::atomic<size_t> next{0};
    uint64_t gen = 0;
    int busy = 0;
    void Work() {
      for (;;) {
        const size_t i0 = next.fetch_add(32);
        if (i0 >= n) break;
        const size_t i1 = std::min(n, i0 + 32);
        for (size_t i = i0; i < i1; i++) fn(i);
      }
    }
    // helpers by machine (players do not have this PC's processor): 3 with 8+ logical processors, 1 with 4-7, none
    // below (the copies then run on the main thread alone); T6_SNAP_THREADS=<n> = n threads in all
    int helpers = 0;
    Pool() {
      const unsigned hc = std::thread::hardware_concurrency();
      helpers = hc >= 8 ? 3 : hc >= 4 ? 1 : 0;
      if (const char* e = T6Env("T6_SNAP_THREADS"); e && *e) helpers = std::max(0, T6ToInt(e) - 1);
      for (int t = 0; t < helpers; t++) {
        std::thread([this] {
          uint64_t seen = 0;
          for (;;) {
            {
              std::unique_lock lock(m);
              cv.wait(lock, [&] { return gen != seen; });
              seen = gen;
            }
            Work();
            {
              std::lock_guard lock(m);
              busy--;
            }
            done_cv.notify_one();
          }
        }).detach();
      }
    }
  };
  static Pool* pool = new Pool;
  if (n < 64 || pool->helpers == 0) {
    for (size_t i = 0; i < n; i++) fn(i);
    return;
  }
  {
    std::lock_guard lock(pool->m);
    pool->fn = fn;
    pool->n = n;
    pool->next = 0;
    pool->busy = pool->helpers;
    pool->gen++;
  }
  pool->cv.notify_all();
  pool->Work();
  std::unique_lock lock(pool->m);
  pool->done_cv.wait(lock, [&] { return pool->busy == 0; });
}

// Allocator whose value-initialization does nothing (resize leaves the new bytes as they are)
template <class T>
struct NoInitAlloc : std::allocator<T> {
  template <class U> struct rebind { using other = NoInitAlloc<U>; };
  NoInitAlloc() = default;
  template <class U> NoInitAlloc(const NoInitAlloc<U>&) {}
  template <class U> void construct(U* p) noexcept { ::new (static_cast<void*>(p)) U; }
  template <class U, class... A> void construct(U* p, A&&... a) { ::new (static_cast<void*>(p)) U(std::forward<A>(a)...); }
};
// 4 KB blocks for page images (chunks of 256, never given back to the system; after a reset every chunk block is
// free again - blocks of the shadow / g_inc_extra that went through the free list are not kept across a reset)
PagePool g_page_pool;
std::deque<IncLog> g_inc_logs;
std::vector<IncLog> g_inc_spare;  // dropped logs, their buffers are reused
std::vector<uint32_t> g_dirty_stat;  // T6_NET_DIRTYSTAT: saves per page
std::unordered_map<uint32_t, std::vector<uint8_t>> g_inc_extra;  // image of saved pages outside the shadow
bool g_inc_have = false;
uint32_t g_inc_last = 0;
uint64_t g_inc_sweep = 0, g_inc_missed = 0, g_inc_pages = 0, g_inc_restored = 0;
// PS3 RB_RSX_HOLD: the load does not wait for the GPU (ring ~1 ms) and leaves the GPU's pages alone (hg1: load
// 1.8-2.4 -> 0.8 ms, fight state equal, no GPU stall). ON by default; T6_LOAD_HOLD_GPU=0 = wait + restore as before.
// Picture NOT checked by eye yet (PS3: a wrong set of held pages showed as flicker of effects).
bool LoadHoldGpu() {
  static const bool on = T6EnvDefaultOn("T6_LOAD_HOLD_GPU");
  return on;
}
uint64_t g_load_held = 0;
double g_inc_resize_ms = 0;  // undo log buffer growth (reallocation + copy of the used part)
uint64_t g_inc_resizes = 0;
uint64_t g_inc_rb_pages = 0, g_inc_rb_saves = 0, g_inc_live_pages = 0, g_inc_live_saves = 0;
bool IncEnabled() {
  static const bool on = T6EnvDefaultOn("T6_SNAP_INC");
  return on;
}
// Per page: its image once it is known (in scope + image present). The save's bookkeeping asked IncInScope and
// IncImage twice for each of ~600 pages per frame (0.4-0.6 ms, rb5); scope and image of a page do not change until
// the next reset.
std::vector<uint8_t*> g_inc_img_cache(0x100000, nullptr);
std::vector<uint8_t> g_inc_in_log(0x100000, 0);  // page handled in the save being made (two passes)
std::vector<uint32_t> g_inc_marked;
IncLog g_inc_pending;
bool g_inc_pending_valid = false;
void IncReset() {
  g_inc_logs.clear();
  g_inc_spare.clear();
  g_page_pool.Reset();
  g_inc_extra.clear();
  g_inc_have = false;
  std::fill(g_inc_img_cache.begin(), g_inc_img_cache.end(), nullptr);
  for (const uint32_t q : g_inc_marked) g_inc_in_log[q] = 0;
  g_inc_marked.clear();
  g_inc_pending_valid = false;
}
// T6_SNAP_NO_PHYS=1 (experiment, PS3: "leave the render data out instead of waiting for the draw thread"): the game's
// pages in the physical range 0x4xxxxxxx (D3D device, command / vertex buffers) are not saved or restored, and a load
// does not wait for the draw thread / GPU to be idle.
bool SnapNoPhys() {
  static const bool on = T6Env("T6_SNAP_NO_PHYS") != nullptr;
  return on;
}
bool IncInScope(uint32_t q) {
  if (q >= 0x40000 && q < 0x50000 && SnapNoPhys()) return false;
  return T6InGameMemory(q) || (q >= 0x40000 && q < 0x50000 && ((t6_pagemap[q] & 0x01) || g_pre_pages[q - 0x40000]));
}
// Image of a page at the last save (nullptr: not known, the page keeps its live content)
uint8_t* IncImage(uint32_t q, bool create) {
  if (const int64_t i = ShadowIndex(q); i >= 0) {
    if (!g_shadow_valid[i]) {
      if (!create) return nullptr;
      g_shadow_valid[i] = 1;  // committed after the shadow: its first saved content becomes its image
    }
    return g_shadow.data() + (static_cast<size_t>(i) << 12);
  }
  auto it = g_inc_extra.find(q);
  if (it != g_inc_extra.end()) return it->second.data();
  if (!create) return nullptr;
  return g_inc_extra.emplace(q, std::vector<uint8_t>(0x1000)).first->second.data();
}
// VirtualQuery costs ~0.1 ms per call in this process (2 x 32 GB reservations): the answer is cached per page and
// asked again after 300 saves (IncSave bumps g_inc_epoch); 0 unknown, 1 readable, 2 not
std::vector<uint8_t> g_inc_readable(0x100000);
uint32_t g_inc_epoch = 0;
bool PageReadable(uint32_t q) {
  uint8_t& c = g_inc_readable[q];
  if (!c) {
    MEMORY_BASIC_INFORMATION mbi{};
    const bool ok = VirtualQuery(g_t6_membase + (static_cast<uint64_t>(q) << 12), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                    !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
    c = ok ? 1 : 2;
    // The answer holds for the whole region VirtualQuery reports: the unknown pages after this one in a readable
    // region take it too. (2026-10-12: at the loading -> intro change the game converts ~40 motions into ~20 MB of
    // new pages in one frame; one VirtualQuery per page made that frame's save 650-700 ms, the "bookkeeping" part -
    // the freeze at the start of the first match.) Only "readable" spreads: a page that is not committed yet is
    // asked again when the game writes it.
    if (ok) {
      const uint64_t end = (reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - g_t6_membase) >> 12;
      for (uint64_t n = static_cast<uint64_t>(q) + 1; n < end && n < g_inc_readable.size() && !g_inc_readable[n]; n++) g_inc_readable[n] = 1;
    }
  }
  return c == 1;
}
// (end of an inner block of namespace t6np)


}  // namespace t6np
