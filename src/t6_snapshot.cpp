// Tekken 6 netplay - save / load of the game state (SnapTest) and the save/restore test (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

namespace t6np {

// PS3 wait_for_draw_idle / RB_SNAP_SAVE_IDLE: the frame must be completely drawn, GPU included, before a save or a
// load. A ring with read == write is not enough: the GPU can still owe a swap (scratch flag *(device[+10900] + 4) = 1,
// WAIT_REG_MEM until the vblank callback sub_824D9B38 clears it) and fences. Restoring then left the GPU waiting for a
// release that never came and the main thread waiting for its fence (st47, st50, st54, st59: D3D 824D3FA0).
// Idle = ring empty, every issued fence written back (device+10908 vs *(device[+10896])), swap queue empty, flag 0.
LARGE_INTEGER g_drawidle_prev{};
double g_drawidle_ms[3] = {};  // load waits: ring not empty, fences not written back, swap pending
bool WaitDrawIdle(int max_ms, const char* what) {
  const auto host = [](uint32_t a) { return a + (a >= 0xE0000000u ? 0x1000u : 0u); };
  constexpr uint32_t dev = 0x40021D80u;
  LARGE_INTEGER f, t0, t1;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);
  g_drawidle_prev = t0;
  for (;;) {
    const uint32_t fence_wb = T6Rd32(dev + 10896), flag_wb = T6Rd32(dev + 10900);
    const bool ring = !g_t6_gpu_idle || g_t6_gpu_idle();
    // at rest the written-back fence is 2 behind the device counter (st60/st61: issued 992B, written 9929)
    const bool fences = !fence_wb || T6Rd32(dev + 10908) - T6Rd32(host(fence_wb)) <= 2;
    const bool swaps = T6Rd32(dev + 16700) == T6Rd32(dev + 16704) && (!flag_wb || T6Rd32(host(flag_wb + 4)) == 0);
    // T6_LOAD_NO_GPU_WAIT=2 (experiment): a load does not wait for the fences, only for the ring and the swap flag (ng1:
    // with no wait at all the GPU stuck in WAIT_REG_MEM on the swap flag that the load had restored)
    static const bool no_fences = T6EnvIs("T6_LOAD_NO_GPU_WAIT", '2');
    QueryPerformanceCounter(&t1);
    {
      // which condition a load waited for (time since the previous poll counted to each one still false)
      LARGE_INTEGER& prev = g_drawidle_prev;
      if (what[0] == 'l' && prev.QuadPart && t1.QuadPart > prev.QuadPart && t1.QuadPart - t0.QuadPart > 0) {
        const double dt = static_cast<double>(t1.QuadPart - prev.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
        if (!ring) g_drawidle_ms[0] += dt;
        if (!fences) g_drawidle_ms[1] += dt;
        if (!swaps) g_drawidle_ms[2] += dt;
      }
      prev = t1;
    }
    if (ring && swaps && (fences || (no_fences && what[0] == 'l'))) return true;
    if ((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart >= max_ms) {
      REXLOG_WARN("T6 snaptest: draw not idle at the {} after {} ms (ring {} fences {} swaps {}; fence issued {:08X} written {:08X})",
                  what, max_ms, ring, fences, swaps, T6Rd32(dev + 10908), fence_wb ? T6Rd32(host(fence_wb)) : 0u);
      return false;
    }
    Sleep(0);
  }
}

std::vector<Range> T6LibBlocks();  // library heap blocks (end of file)
double g_save_parts[4] = {};  // ms: net worker idle, job workers idle, calibration, copy
double g_save_first_ms = 0;
bool g_in_rollback = false;     // between a load and the end of the boundary
double g_rbsave[4] = {};        // saves inside rollbacks: draw worker lock, job workers, global lock, copy   // first pass of live saves (before the wait for the draw worker)
uint64_t g_save_bytes = 0;
SnapTest::SnapTest(bool ring_slot) : quiet(ring_slot) {
  if (const char* p = T6Env("T6_SNAPTEST"); !ring_slot && p && *p) {
    char* e = nullptr;
    at = std::strtoul(p, &e, 0);
    frames = (*e == ',') ? std::strtoul(e + 1, nullptr, 0) : 120;
    phase = 0;
    REXLOG_INFO("T6 snaptest: save at step {}, {} frames", at, frames);
  }
}

void SnapTest::Save(uint32_t frame) {
  LARGE_INTEGER q0, q1, q2, q3, q4;
  QueryPerformanceCounter(&q0);
  // OFF by default (T6_NET_SAVE_TWO_PASS=1): correct (rb9, rb10: fight state equal) but no gain - with the audio step
  // at the boundary the worker is done before the save anyway (+0.3 ms for the second scan), and with the audio
  // step moved into the draw the wait for the worker is simply the last link of the chain.
  if (static const bool two = T6Env("T6_NET_SAVE_TWO_PASS") != nullptr; two && quiet && IncEnabled() && !T6NetResim()) {
    g_net_where = "save: first pass";
    const double f0 = T6NowMs();
    GlobalLockHold global;
    IncFirstPass();
    g_save_first_ms += T6NowMs() - f0;
  }
  g_net_where = "save: draw worker lock";
  NetLockHold net(quiet);
  QueryPerformanceCounter(&q1);
  g_net_where = "save: job workers";
  if (!WaitWorkersIdle(50)) REXLOG_WARN("T6 snaptest: job workers still busy at the save");
  QueryPerformanceCounter(&q2);
  g_net_where = "save: copy";
  // The GPU only matters at a load (it reads memory the load rewrites); a save copies what the main thread and the
  // (idle) workers wrote. PS3: waiting for the draw thread before every save was the main cost (RB_SNAP_SAVE_IDLE).
  if (!quiet) WaitDrawIdle(100, "save");
  if (quiet && IncEnabled()) {
    LARGE_INTEGER g0, g1;
    QueryPerformanceCounter(&g0);
    GlobalLockHold global;
    QueryPerformanceCounter(&g1);
    IncSave(frame, q2);
    g_save_parts[0] += Ms(q0, q1);
    g_save_parts[1] += Ms(q1, q2);
    if (g_in_rollback) {
      LARGE_INTEGER e;
      QueryPerformanceCounter(&e);
      g_rbsave[0] += Ms(q0, q1);
      g_rbsave[1] += Ms(q1, q2);
      g_rbsave[2] += Ms(g0, g1);
      g_rbsave[3] += Ms(g1, e);
    }
    return;
  }
  if (quiet) {
    static uint32_t saves = 0;
    CalibrateFromMap(SnapRanges(), g_cal_exclude, saves++ % 30 == 0);
    g_cal_done = true;
  } else if (T6Env("T6_SNAP_FROM_MAP")) {
    CalibrateFromMap(SnapRanges(), g_cal_exclude);
    g_cal_done = true;
    REXLOG_INFO("T6 snaptest: calibrated in session: {} ranges, {} excluded runs", SnapRanges().size(), g_cal_exclude.size());
    // pages game code wrote outside the XEX data and the game heaps (not saved by the game-memory rule)
    std::string other;
    for (uint32_t pg = 0x30000; pg < 0x70000;) {
      if (!(t6_pagemap[pg] & 0x01)) { pg++; continue; }
      uint32_t end = pg + 1;
      while (end < 0x70000 && (t6_pagemap[end] & 0x01)) end++;
      other += fmt::format(" {:08X}-{:08X}/{:X}", pg << 12, end << 12, t6_pagemap[pg]);
      pg = end;
    }
    REXLOG_INFO("T6 snaptest: game-written pages outside game memory:{}", other);
  }
  snap.clear();
  bytes = 0;
  // Committed pages of the snapshot ranges: VirtualQuery over thousands of ranges cost ~110 ms per save (v6); the
  // pages are written by the game, so their commit state changes rarely -> refreshed every 60 saves (ring slots)
  static std::vector<uint8_t> committed(0x100000);
  static uint32_t refresh = 0;
  const bool fresh = !quiet || refresh++ % 60 == 0;
  const auto for_each = [&](uint32_t lo, uint32_t hi, const auto& fn) {
    if (fresh) {
      for (uint32_t p = lo >> 12; p < (hi + 0xFFF) >> 12; p++) committed[p] = 0;
      ForEachCommitted(lo, hi, [&](uint32_t a, uint32_t n) {
        for (uint32_t p = a >> 12; p < (a + n + 0xFFF) >> 12; p++) committed[p] = 1;
      });
    }
    for (uint32_t p = lo >> 12; p < (hi + 0xFFF) >> 12;) {
      if (!committed[p] && !fresh) {
        // a page that joined the ranges since the last refresh (rare): check it alone
        MEMORY_BASIC_INFORMATION mbi{};
        committed[p] = VirtualQuery(g_t6_membase + (static_cast<uint64_t>(p) << 12), &mbi, sizeof(mbi)) &&
                       mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
      }
      if (!committed[p]) { p++; continue; }
      uint32_t e = p + 1;
      while (e < (hi + 0xFFF) >> 12 && committed[e]) e++;
      const uint32_t a = std::max(lo, p << 12), b = std::min(hi, e << 12);
      if (b > a) fn(a, b - a);
      p = e;
    }
  };
  // total size first, then one copy pass into the reused buffer
  size_t total = 0;
  for (const auto& r : SnapRanges()) for_each(r.lo, r.hi, [&](uint32_t, uint32_t n) { total += n; });
  if (buf.size() < total + 0x1000) buf.resize((total + 0x1000) * 5 / 4);
  QueryPerformanceCounter(&q3);
  saved_pages.assign(g_shadow_valid.size(), 0);
  for (const auto& r : SnapRanges()) {
    for_each(r.lo, r.hi, [&](uint32_t a, uint32_t n) {
      if (bytes + n > buf.size()) return;  // the committed set grew since the sizing pass
      std::memcpy(buf.data() + bytes, g_t6_membase + a, n);
      snap.push_back({a, n, bytes});
      bytes += n;
      for (uint32_t p = a >> 12; p < (a + n + 0xFFF) >> 12; p++) {
        if (const int64_t i = ShadowIndex(p); i >= 0 && i < static_cast<int64_t>(saved_pages.size())) saved_pages[i] = 1;
      }
    });
  }
  // PS3: the main thread's TLS (CRT rand state) must be restored. X360 CRT per-thread data of the main thread
  // (srand stores to _getptd()+20, CRT rand reads it): add it when the page rule left it out.
  if (g_main_ctx) {
    const uint32_t ptd = T6MainPtd();
    bool covered = false;
    for (const auto& reg : snap) covered |= ptd >= reg.addr && ptd + 0x200 <= reg.addr + reg.size;
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("T6 snaptest: CRT per-thread data of the main thread {:08X} (page kind {:X}) in the snapshot: {}", ptd,
                  t6_pagemap[ptd >> 12], covered);
    }
    if (ptd && !covered && bytes + 0x200 <= buf.size()) {
      std::memcpy(buf.data() + bytes, g_t6_membase + ptd, 0x200);
      snap.push_back({ptd, 0x200, bytes});
      bytes += 0x200;
    }
  }
  QueryPerformanceCounter(&q4);
  g_save_parts[0] += Ms(q0, q1);
  g_save_parts[1] += Ms(q1, q2);
  g_save_parts[2] += Ms(q2, q3);
  g_save_parts[3] += Ms(q3, q4);
  g_save_bytes += bytes;
}

double* SnapTest::IncTp() {
  static double tp[5] = {};
  return tp;
}

IncLog SnapTest::IncNewLog() {
  IncLog log;
  if (!g_inc_spare.empty()) {  // buffers of a dropped log: no 2-3 MB allocation + first-touch faults per frame
    log = std::move(g_inc_spare.back());
    g_inc_spare.pop_back();
    log.pages.clear();
    log.bufs.clear();
    log.ptd.clear();
    log.ptd_addr = 0;
  }
  return log;
}

const std::vector<uint32_t>& SnapTest::IncCopyDirty(IncLog& log) {
  double* const tp = IncTp();
  const double tp0 = T6NowMs();
  static std::vector<uint32_t> dirty;
  dirty.clear();
  // (the level-1 byte is cleared before its pages: a store of another thread in between sets it again)
  const uint64_t* w = reinterpret_cast<const uint64_t*>(t6_dirty);
  for (uint32_t g = 0; g < sizeof(t6_dirty_l1); g++) {
    if (!t6_dirty_l1[g]) continue;
    t6_dirty_l1[g] = 0;
    for (uint32_t k = g * 8; k < g * 8 + 8; k++) {
      if (!w[k]) continue;
      for (uint32_t b = 0; b < 8; b++) {
        const uint32_t q = k * 8 + b;
        if (t6_dirty[q]) { t6_dirty[q] = 0; dirty.push_back(q); }
      }
    }
  }
  log.pages.reserve(log.pages.size() + dirty.size() + 64);
  {
    // refresh of the readable cache: one dirty page every 8 saves is asked again (2 per save = 2 VirtualQuery calls
    // of ~0.1-0.2 ms each in this process: the "bookkeeping" of a save jumped 0.06 -> 0.43 ms in some windows, sw1)
    static uint32_t rr = 0, n = 0;
    if (!dirty.empty() && (n++ & 7) == 0) g_inc_readable[dirty[rr++ % dirty.size()]] = 0;
  }
  const double tp1 = T6NowMs();
  tp[0] += tp1 - tp0;
  // PS3 page store: the copies run on 4 threads (RB_SNAP_THREADS). First the bookkeeping on this thread (scope, image
  // buffer, undo log), then ONE 4 KB copy per page in parallel (memory -> its new image buffer).
  struct Item { uint8_t* img; const uint8_t* mem; };
  static std::vector<Item> items;
  items.clear();
  for (const uint32_t q : dirty) {
    uint8_t* img = g_inc_img_cache[q];
    bool known = img != nullptr;
    if (known) {
      if (!PageReadable(q)) continue;
    } else {
      if (!IncInScope(q) || !PageReadable(q)) continue;
      known = IncImage(q, false) != nullptr;
      img = IncImage(q, true);
      g_inc_img_cache[q] = img;
    }
    // (a page already handled by the first pass of this save keeps its logged content / stays unlogged)
    const bool logged = known && g_inc_have && !g_inc_in_log[q];
    if (logged) {
      // the old image goes to the log as it is; the page gets a fresh buffer
      log.pages.push_back(q);
      log.bufs.push_back(img);
      img = g_page_pool.Get();
      g_inc_img_cache[q] = img;
    }
    if (!g_inc_in_log[q]) {
      g_inc_in_log[q] = 1;
      g_inc_marked.push_back(q);
    }
    items.push_back({img, g_t6_membase + (static_cast<size_t>(q) << 12)});
    g_inc_pages++;
  }
  const double tp2 = T6NowMs();
  tp[1] += tp2 - tp1;
  T6ParallelFor(items.size(), [](size_t i) {
    const Item& it = items[i];
    std::memcpy(it.img, it.mem, 0x1000);
  });
  tp[2] += T6NowMs() - tp2;
  return dirty;
}

void SnapTest::IncFirstPass() {
  g_inc_pending = IncNewLog();
  g_inc_pending_valid = true;
  IncCopyDirty(g_inc_pending);
}

void SnapTest::IncSave(uint32_t frame, const LARGE_INTEGER& q2) {
  static uint32_t saves = 0;
  // (the readable cache is refreshed a few pages per save in the copy loop: the old reset of the whole cache every
  // 300 saves cost ~650 VirtualQuery calls = a 60 ms frame every 5 s)
  // live-byte exclusions of the fine map (the page set itself comes from t6_dirty); live saves only - a rollback
  // does not pay for it (0.17 ms per save on average)
  // ... and only when the fine maps got a new kind somewhere (t6_fine_changed, t6_mark): a calibration costs ~5 ms
  // (xm1: 0.17 ms per save on average = a 5 ms longer frame twice a second)
  CalibrateAsyncTake();
  if (!g_cal_done) {
    CalibrateFromMap(SnapRanges(), g_cal_exclude, true);
    g_cal_done = true;
    std::memset(t6_fine_page, 0, sizeof(t6_fine_page));
    g_excl_by_page.clear();
    g_excl_extra.clear();
  } else if (!T6NetResim() && saves++ % 30 == 0 && t6_fine_changed) {
    t6_fine_changed = 0;
    // Only the pages whose fine bytes changed (calibration 0.13 -> 0.003 ms per save). ON by default: st1 / st2 ran
    // without the 0.3-2.5 s stalls of cf2 / cf3 (most likely a load on the PC then, not this). T6_EXCL_INC=0 = the
    // full calibration on the main thread.
    static const bool inc = T6EnvDefaultOn("T6_EXCL_INC");
    // T6_CAL_ASYNC=1: on a helper thread (ca1: the whole run 3-10x slower, resim step 14 ms - cause open) -> off
    static const bool async = T6Env("T6_CAL_ASYNC") != nullptr;
    if (inc) RefreshExclusionsIncremental();
    else if (async) CalibrateAsyncStart();
    else CalibrateFromMap(SnapRanges(), g_cal_exclude, true);
  }
  LARGE_INTEGER q3, q4;
  QueryPerformanceCounter(&q3);
  IncLog log;
  if (g_inc_pending_valid) {  // first pass done before the wait for the draw worker (IncFirstPass)
    log = std::move(g_inc_pending);
    g_inc_pending_valid = false;
  } else {
    log = IncNewLog();
  }
  log.frame = frame;
  const auto take = [&](uint32_t q) {
    if (!IncInScope(q) || !PageReadable(q)) return;
    uint8_t* img = g_inc_img_cache[q];
    const bool known = img != nullptr || IncImage(q, false) != nullptr;
    if (!img) img = g_inc_img_cache[q] = IncImage(q, true);
    if (known && g_inc_have) {
      log.pages.push_back(q);
      log.bufs.push_back(img);
      img = g_inc_img_cache[q] = g_page_pool.Get();
    }
    std::memcpy(img, g_t6_membase + (static_cast<size_t>(q) << 12), 0x1000);
    g_inc_pages++;
  };
  static double* const tp = IncTp();  // ms: dirty map scan, bookkeeping, page copies, sweep, rest
  const bool second_pass = !log.pages.empty() || g_inc_marked.size() > 0;
  const std::vector<uint32_t>& late = IncCopyDirty(log);
  if (g_in_rollback) { g_inc_rb_pages += late.size(); g_inc_rb_saves++; }
  else { g_inc_live_pages += late.size(); g_inc_live_saves++; }
  // T6_NET_TAILSTAT=1 (with T6_NET_AUDIO_ASYNC=1): which pages are written between the first pass and the end of the
  // draw worker's work = what the worker (and any other thread) writes after the main thread is done with the
  // frame. At session frame 2000: per 1 MB region, pages by writer since the session start (t6_pagemap 0x40 = main
  // thread, 0x20 = another thread) -> can a save / a rollback go on without waiting for the worker?
  if (static const bool ts = T6Env("T6_NET_TAILSTAT") != nullptr; ts && second_pass) {
    static std::vector<uint16_t> tail(0x100000, 0);
    static uint64_t saves = 0, pages = 0;
    for (const uint32_t q : late) { if (tail[q] < 0xFFFF) tail[q]++; }
    saves++;
    pages += late.size();
    if (frame == 2000) {
      std::map<uint32_t, std::array<uint32_t, 4>> reg;  // region -> pages: other-only, both, main-only(?), writes
      for (uint32_t q = 0; q < 0x100000; q++) {
        if (!tail[q]) continue;
        auto& r = reg[q >> 8];
        const uint8_t k = t6_pagemap[q];
        r[(k & 0x40) ? ((k & 0x20) ? 1 : 2) : 0]++;
        r[3] += tail[q];
      }
      REXLOG_INFO("T6 tail: {} saves with a second pass, {:.1f} pages per save written after the first pass", saves,
                  static_cast<double>(pages) / saves);
      for (const auto& [r, c] : reg) {
        REXLOG_INFO("T6 tail: region {:08X}: {} pages written only by other threads, {} by both, {} only by the main thread; {} "
                    "late writes", r << 20, c[0], c[1], c[2], c[3]);
      }
    }
  }
  // the marks of this save (logged once / not to be logged) are taken back
  for (const uint32_t q : g_inc_marked) g_inc_in_log[q] = 0;
  g_inc_marked.clear();
  const double tp3 = T6NowMs();
  // T6_NET_DIRTYSTAT=1: which pages are saved how often -> <dump dir>/dirty_stat.txt at session frames 1500 and 3000
  // (per page: saves in the last window; the candidates to leave out: render output, cosmetic pools)
  if (static const bool ds = T6Env("T6_NET_DIRTYSTAT") != nullptr; ds) {
    if (g_dirty_stat.empty()) g_dirty_stat.assign(0x100000, 0);
    for (const uint32_t q : log.pages) g_dirty_stat[q]++;
    if (frame == 1500 || frame == 3000) {
      const char* d = T6Env("T6_NET_DUMP_DIR");
      if (FILE* f = T6Open(fmt::format("{}/dirty_stat_{}.txt", d ? d : ".", frame), "w")) {
        for (uint32_t q = 0; q < 0x100000; q++) {
          if (g_dirty_stat[q]) std::fprintf(f, "%08X %u %d\n", q << 12, g_dirty_stat[q], IncInScope(q) ? 1 : 0);
        }
        std::fclose(f);
      }
      std::fill(g_dirty_stat.begin(), g_dirty_stat.end(), 0);
    }
  }
  // safety sweep (PS3 RB_SNAP_SWEEP): 64 shadow pages per save compared with their image
  static uint32_t sweep_pos = 0;
  const uint32_t npages = static_cast<uint32_t>(g_shadow_valid.size());
  for (uint32_t k = 0; k < 64 && npages; k++) {
    const uint32_t i = sweep_pos++ % npages;
    if (!g_shadow_valid[i]) continue;
    const uint32_t q = ShadowPageAddr(i) >> 12;
    if (!IncInScope(q) || !t6_pagemap[q] || !PageReadable(q)) continue;
    g_inc_sweep++;
    const uint8_t* img = g_inc_img_cache[q] ? g_inc_img_cache[q] : g_shadow.data() + (static_cast<size_t>(i) << 12);
    if (std::memcmp(img, g_t6_membase + (static_cast<size_t>(q) << 12), 0x1000) != 0) {
      if (g_inc_missed++ < 20) REXLOG_WARN("T6 snap: page {:08X} changed without a mark in the dirty map (frame {})", q << 12, frame);
      take(q);
    }
  }
  const double tp4 = T6NowMs();
  tp[3] += tp4 - tp3;
  // CRT per-thread block of the main thread (system heap, outside the pages): saved with every frame
  if (g_main_ctx) {
    log.ptd_addr = T6MainPtd();
    if (log.ptd_addr) log.ptd.assign(g_t6_membase + log.ptd_addr, g_t6_membase + log.ptd_addr + 0x200);
  }
  bytes = log.pages.size() << 12;
  g_inc_logs.push_back(std::move(log));
  while (g_inc_logs.size() > 64) {
    IncLog& old = g_inc_logs.front();
    for (uint8_t* b : old.bufs) g_page_pool.Put(b);  // nobody can go back that far any more
    old.bufs.clear();
    if (g_inc_spare.size() < 8) g_inc_spare.push_back(std::move(old));
    g_inc_logs.pop_front();
  }
  g_inc_have = true;
  g_inc_last = frame;
  QueryPerformanceCounter(&q4);
  g_save_parts[2] += Ms(q2, q3);
  g_save_parts[3] += Ms(q3, q4);
  g_save_bytes += bytes;
  tp[4] += T6NowMs() - tp4;
  if (static uint32_t n = 0; n++ % 600 == 0) {
    REXLOG_INFO("T6 snap: incremental, frame {}: {} pages saved so far, {} restored, sweep {} pages / {} missed", frame,
                g_inc_pages, g_inc_restored, g_inc_sweep, g_inc_missed);
    REXLOG_INFO("T6 snap: copy part per save (ms): dirty map scan {:.3f}, bookkeeping {:.3f}, page copies {:.3f}, sweep + stats {:.3f}, "
                "rest {:.3f}", tp[0] / 600, tp[1] / 600, tp[2] / 600, tp[3] / 600, tp[4] / 600);
    for (int k = 0; k < 5; k++) tp[k] = 0;
  }
}

bool SnapTest::IncLoad(uint32_t t) const {
  if (!g_inc_have || g_inc_logs.empty() || g_inc_logs.front().frame > t + 1 || t > g_inc_last) {
    REXLOG_ERROR("T6 snap: frame {} not in the incremental store (frames {}-{})", t,
                 g_inc_logs.empty() ? 0u : g_inc_logs.front().frame, g_inc_last);
    return false;
  }
  // PS3 page store: the restore copies run on 4 threads like the save's. First, on this thread, the final source of
  // every page is worked out: a page written since the last save comes back from the image; a page in the undo logs
  // of the frames after t gets the OLDEST of its logged contents (the logs are walked newest first, each one
  // overrides) and the image follows it. Then one pass of independent 4 KB copies.
  static std::vector<uint8_t> in_set(0x100000, 0);
  static std::vector<uint32_t> pages;
  pages.clear();
  const uint64_t* w = reinterpret_cast<const uint64_t*>(t6_dirty);
  for (uint32_t k = 0; k < sizeof(t6_dirty) / 8; k++) {
    if ((k & 7) == 0) {
      if (!t6_dirty_l1[k >> 3]) { k += 7; continue; }
      t6_dirty_l1[k >> 3] = 0;
    }
    if (!w[k]) continue;
    for (uint32_t b = 0; b < 8; b++) {
      const uint32_t q = k * 8 + b;
      if (!t6_dirty[q]) continue;
      t6_dirty[q] = 0;
      if (!g_inc_img_cache[q] && (!IncInScope(q) || !IncImage(q, false))) continue;
      in_set[q] = 1;
      pages.push_back(q);
      g_inc_restored++;
    }
  }
  // the logs of the frames after t, newest first: each gives its buffers back as the image of their page (the image
  // they replace goes to the pool), so after the oldest one the table holds the state of t
  while (!g_inc_logs.empty() && g_inc_logs.back().frame > t) {
    IncLog& l = g_inc_logs.back();
    for (size_t k = 0; k < l.pages.size(); k++) {
      const uint32_t q = l.pages[k];
      if (uint8_t* cur = g_inc_img_cache[q]) g_page_pool.Put(cur);
      g_inc_img_cache[q] = l.bufs[k];
      if (!in_set[q]) {
        in_set[q] = 1;
        pages.push_back(q);
      }
      g_inc_restored++;
    }
    l.bufs.clear();
    if (g_inc_spare.size() < 8) g_inc_spare.push_back(std::move(l));
    g_inc_logs.pop_back();
  }
  struct Item { uint8_t* mem; const uint8_t* img; };
  static std::vector<Item> items;
  items.clear();
  for (const uint32_t q : pages) {
    // PS3 RB_RSX_HOLD: pages of the physical range written only by other threads since the session start (the draw
    // worker's command buffers / render data, which the GPU may still be reading) are not restored; they are
    // marked dirty so the next save takes their live content as the image.
    if (LoadHoldGpu() && q >= 0x40000 && q < 0x50000 && (t6_pagemap[q] & 0x60) == 0x20) {
      in_set[q] = 0;
      t6_dirty_mark(q << 12);
      g_load_held++;
      continue;
    }
    uint8_t* mem = g_t6_membase + (static_cast<size_t>(q) << 12);
    const uint8_t* img = g_inc_img_cache[q] ? g_inc_img_cache[q] : IncImage(q, false);
    in_set[q] = 0;
    if (!img) continue;
    if (g_inc_readable[q] == 1) {  // committed read/write (PageReadable): no protection change needed
      items.push_back({mem, img});
      continue;
    }
    DWORD old = 0;
    if (VirtualProtect(mem, 0x1000, PAGE_READWRITE, &old)) {
      std::memcpy(mem, img, 0x1000);
      VirtualProtect(mem, 0x1000, old, &old);
    }
  }
  T6ParallelFor(items.size(), [](size_t i) {
    const Item& it = items[i];
    std::memcpy(it.mem, it.img, 0x1000);
  });
  if (!g_inc_logs.empty() && g_inc_logs.back().frame == t) {
    const IncLog& l = g_inc_logs.back();
    if (l.ptd_addr && !l.ptd.empty()) std::memcpy(g_t6_membase + l.ptd_addr, l.ptd.data(), 0x200);
  }
  g_inc_last = t;
  return true;
}

void SnapTest::DiffToFile(const char* path, uint32_t frame) const {
  FILE* f = T6Open(path, "a");
  if (!f) return;
  size_t total = 0;
  for (const auto& reg : snap) {
    const uint8_t* m = g_t6_membase + reg.addr;
    uint32_t run_lo = 0, run_hi = 0;
    bool in = false;
    for (uint32_t o = 0; o < reg.size; o += 16) {
      if (std::memcmp(m + o, Data(reg) + o, 16) != 0) {
        if (in && o - run_hi <= 256) {
          run_hi = o + 16;
        } else {
          if (in) std::fprintf(f, "%u %08X-%08X %u\n", frame, reg.addr + run_lo, reg.addr + run_hi, run_hi - run_lo);
          in = true;
          run_lo = o;
          run_hi = o + 16;
        }
        total += 16;
      }
    }
    if (in) std::fprintf(f, "%u %08X-%08X %u\n", frame, reg.addr + run_lo, reg.addr + run_hi, run_hi - run_lo);
  }
  std::fclose(f);
  REXLOG_INFO("T6 snaptest: frame {}: {} KB differ from the snapshot (ranges in {})", frame, total >> 10, path);
}

bool SnapTest::Load(uint32_t frame) const {
  // timing of the parts of a load (net worker lock, job workers, draw/GPU idle, restore), logged every 30 loads
  static double parts[4] = {};
  static uint32_t loads = 0;
  LARGE_INTEGER l0, l1, l2, l3, l4;
  QueryPerformanceCounter(&l0);
  struct PartsLog {
    double* parts; uint32_t* loads; LARGE_INTEGER* l3;
    ~PartsLog() noexcept {
      try {
      LARGE_INTEGER e;
      QueryPerformanceCounter(&e);
      parts[3] += Ms(*l3, e);
      if (++*loads <= 20) {
        REXLOG_INFO("T6 snap: load parts (ms): net worker {:.2f}, job workers {:.2f}, draw idle {:.2f}, restore {:.2f}",
                    parts[0], parts[1], parts[2], parts[3]);
      }
      for (int k = 0; k < 4; k++) parts[k] = 0;
      } catch (...) {  // (a destructor does not throw: a failed log line is dropped)
        T6SwallowedExceptions()++;
      }
    }
  };
  g_net_where = "load: draw worker lock";
  NetLockHold net;
  QueryPerformanceCounter(&l1);
  g_net_where = "load: job workers";
  if (!WaitWorkersIdle(50)) REXLOG_WARN("T6 snaptest: job workers still busy at the load");
  QueryPerformanceCounter(&l2);
  g_net_where = "load: draw / GPU idle";
  // Restoring physical memory while the GPU still reads command buffers there broke the GPU ring
  // T6_LOAD_NO_GPU_WAIT=1 (experiment, PS3 RB_DRAW_IDLE_RSX=1 for loads): no wait for the GPU (ring / fences / swaps)
  // before an incremental load; the draw worker itself is idle (NetLockHold above). The GPU may still read vertex
  // data in the game heaps that the load rewrites (PS3: visual artifacts until the GPU-read pages were held back).
  static const bool no_gpu_wait = T6EnvIs("T6_LOAD_NO_GPU_WAIT", '1');
  if (!((SnapNoPhys() || no_gpu_wait || LoadHoldGpu()) && quiet && IncEnabled())) WaitDrawIdle(100, "load");
  QueryPerformanceCounter(&l3);
  g_net_where = "load: restore";
  GlobalLockHold global;  // from the copy of the kept bytes to their write-back
  parts[0] += Ms(l0, l1);
  parts[1] += Ms(l1, l2);
  parts[2] += Ms(l2, l3);
  PartsLog parts_log{parts, &loads, &l3};
  (void)l4;
  // Live kernel objects (critical sections, dispatcher headers linked to host objects) keep their current bytes:
  // restoring an old owner / handle hangs the game (PS3 port: lwmutex). Registry in rexruntime.dll (xobject.cpp).
  const auto live_fn = T6Rex().live_guest_objects;
  std::vector<uint32_t> live;
  std::vector<uint8_t> live_bytes;
  if (live_fn) {
    const uint32_t n = live_fn(nullptr, 0);
    live.resize(n * 2 + 64);
    const uint32_t m = std::min<uint32_t>(live_fn(live.data(), n + 32), n + 32);
    live.resize(m * 2);
    for (uint32_t k = 0; k < m; k++) {
      const uint8_t* p = g_t6_membase + live[k * 2];
      live_bytes.insert(live_bytes.end(), p, p + live[k * 2 + 1]);
    }
  }
  // T6_SNAP_EXCLUDE_FILE=<file of lo-hi lines> (PS3 RB_SNAP_EXCLUDE): bytes that keep their current value, e.g. the
  // sound library's objects inside the game heaps (fine map of the pagemap build)
  static const std::vector<Range> excl = [] {
    std::vector<Range> v;
    if (const char* p = T6Env("T6_SNAP_EXCLUDE_FILE"); p && *p) {
      if (FILE* f = T6Open(p, "r")) {
        char line[64];
        while (std::fgets(line, sizeof(line), f)) {
          auto r = ParseRanges(line);
          v.insert(v.end(), r.begin(), r.end());
        }
        std::fclose(f);
      }
      REXLOG_INFO("T6 snaptest: {} excluded ranges from {}", v.size(), p);
    }
    return v;
  }();
  std::vector<Range> keep = excl;
  if (g_cal_done) {
    keep.insert(keep.end(), g_cal_exclude.begin(), g_cal_exclude.end());
    keep.insert(keep.end(), g_excl_extra.begin(), g_excl_extra.end());
  }
  {
    const auto& crowd = T6CrowdKeep();
    keep.insert(keep.end(), crowd.begin(), crowd.end());
  }
  // (the incremental load only touches pages in scope: the library heap blocks are in the system heap, outside of it,
  // so copying them out and back - megabytes per load - is only needed for the full restore)
  if (!(quiet && IncEnabled())) {
    const auto lib = T6LibBlocks();
    keep.insert(keep.end(), lib.begin(), lib.end());
  }
  std::vector<uint8_t> excl_bytes;
  for (const auto& r : keep) excl_bytes.insert(excl_bytes.end(), g_t6_membase + r.lo, g_t6_membase + r.hi);
  bool loaded = true;
  // (full restore: the pages stay writable until the kept bytes below are back too, then each run of pages gets its own
  // protection back - VirtualProtect's "old" is the first page's only: a region starting in read-only XEX data made
  // the whole .data read-only, the game's next store crashed; practice position load, all1 / all2 2026-10-19)
  std::vector<std::pair<uint32_t, std::pair<uint32_t, DWORD>>> reprotect;  // address, (size, protection)
  if (quiet && IncEnabled()) {
    loaded = IncLoad(frame);
  } else {
  // Pages first written after the save: their content at the save was the shadow content
  uint32_t from_shadow = 0;
  for (uint32_t i = 0; i < saved_pages.size(); i++) {
    if (saved_pages[i] || !g_shadow_valid[i]) continue;
    const uint32_t a = ShadowPageAddr(i);
    if (!t6_pagemap[a >> 12]) continue;
    std::memcpy(g_t6_membase + a, g_shadow.data() + (static_cast<size_t>(i) << 12), 0x1000);
    from_shadow++;
  }
  if (from_shadow && !quiet) REXLOG_INFO("T6 snaptest: {} pages first written after the save restored from the shadow", from_shadow);
  for (const auto& reg : snap) {
    for (uint32_t a = reg.addr; a < reg.addr + reg.size;) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (!VirtualQuery(g_t6_membase + a, &mbi, sizeof(mbi))) break;
      const uint64_t end = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<uint64_t>(g_t6_membase);
      const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(end, reg.addr + reg.size) - a);
      if (n == 0) break;
      if (mbi.State == MEM_COMMIT) reprotect.push_back({a, {n, mbi.Protect}});
      a += n;
    }
    DWORD old = 0;
    if (VirtualProtect(g_t6_membase + reg.addr, reg.size, PAGE_READWRITE, &old)) {
      std::memcpy(g_t6_membase + reg.addr, Data(reg), reg.size);
    } else {
      REXLOG_ERROR("T6 snaptest: region {:08X}+{:X} could not be made writable ({}): not restored", reg.addr, reg.size, GetLastError());
      loaded = false;  // (a partial restore is no restore - review)
    }
  }
  }
  size_t eoff = 0;
  for (const auto& r : keep) {
    // (unchanged bytes are not written: a kept range outside the restored regions may sit in a read-only page)
    if (std::memcmp(g_t6_membase + r.lo, excl_bytes.data() + eoff, r.hi - r.lo) != 0) {
      // (a changed kept range outside the restored regions may still sit in a read-only page - review)
      DWORD old = 0;
      const bool unlocked = VirtualProtect(g_t6_membase + r.lo, r.hi - r.lo, PAGE_READWRITE, &old) != 0;
      std::memcpy(g_t6_membase + r.lo, excl_bytes.data() + eoff, r.hi - r.lo);
      if (unlocked && old != PAGE_READWRITE) VirtualProtect(g_t6_membase + r.lo, r.hi - r.lo, old, &old);
    }
    eoff += r.hi - r.lo;
  }
  size_t off = 0;
  for (size_t k = 0; k + 1 < live.size(); k += 2) {
    if (std::memcmp(g_t6_membase + live[k], live_bytes.data() + off, live[k + 1]) != 0)
      std::memcpy(g_t6_membase + live[k], live_bytes.data() + off, live[k + 1]);
    off += live[k + 1];
  }
  for (const auto& [a, run] : reprotect) {
    DWORD was = 0;
    if (run.second != PAGE_READWRITE) VirtualProtect(g_t6_membase + a, run.first, run.second, &was);
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("T6 snaptest: {} live kernel objects kept ({})", live.size() / 2,
                live_fn ? "rexruntime registry" : "registry NOT FOUND");
  }
  return loaded;
}


SnapTest& snaptest() {
  static SnapTest t;
  return t;
}

// Hash of everything the snapshot covers, per range (for the snapshot test)
std::vector<uint64_t> FullHashes() {
  std::vector<uint64_t> out;
  for (const auto& r : SnapRanges()) {
    uint64_t h = 0xcbf29ce484222325ull;
    ForEachCommitted(r.lo, r.hi, [&](uint32_t a, uint32_t n) { h = (h ^ Fnv(g_t6_membase + a, n)) * 0x100000001b3ull; });
    out.push_back(h);
  }
  return out;
}

double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  return (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
}

}  // namespace t6np
