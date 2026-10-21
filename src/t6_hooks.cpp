// Tekken 6 netplay - frame boundary and input hooks (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// Frame boundary (PS3: rb_frame_step in place of the task tree walk FUN_00100250 called from the main loop): midasm hook
// T6FrameHook at 0x82219900, the head of the main loop sub_822198D8 `while (running) sub_82218C80(root)`, the only
// caller of the per-frame task tree step. Nothing of the frame is on the main thread's stack here (no held locks, no
// task pointers), so the state is saved and loaded only here.
// Save/restore test. Pass 1: steps at .. at+frames-1 (inputs + hashes recorded at the input hook). At the boundary
// after frames steps the state of the boundary before step `at` is loaded, and pass 2 replays the same inputs.
void T6FrameHook(PPCRegister& r3) {
  T6SpikeFrameEnd();
  if (static const bool prof = T6Env("T6_PROFILE") != nullptr; prof && !g_prof_main.load()) {
    HANDLE h = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    g_prof_main.store(h);
    std::thread(ProfileThread).detach();
  }
  (void)r3;
  if (!g_t6_membase) return;
  PracticePace();
  StanceAudit();
  FightPlayerDump();
  FrameAdvantageTick();
  PracticeTick();
  if (!g_main_ctx) {
    const auto fn = T6Rex().current_guest_context;
    if (fn) g_main_ctx = static_cast<PPCContext*>(fn());
  }
  // The practice gets the session's hang watch too (user 2026-10-20: the practice froze ~2 s after the 17th position
  // load, nothing in the log - main thread position, critical section owner chain and GPU fences when it stops); it
  // watches only while the practice battle exists (menus and loading in between are not watched)
  if (static const bool practice = T6Env("T6_PRACTICE") != nullptr; practice) {
    static uint32_t practice_boundaries = 0;
    practice_boundaries++;
    g_watch_pause = !g_practice_battle;
    if (g_practice_battle) StartHangWatch(&practice_boundaries);
  }
  t6_thread_bit = 0;
  // Online matches are first to 3 rounds (user 2026-10-11: "musi to byt na 3 vitezne kola"; the game's versus default
  // is 2). T6_ROUNDS / 'rounds=' of the start line still override it.
  if (static bool once = false; !once) {
    once = true;
    if (T6Env("T6_NET") && !T6Env("T6_ROUNDS")) T6SetParam("T6_ROUNDS", "3");
  }
  T6CrowdClockTick();
  T6PoseDiagFrame();
  T6NetHold();
  if (T6NetFrame()) return;
  T6BattleDump();
  auto& s = state();
  auto& t = snaptest();
  if (t.phase == 0 && s.step == t.at) {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    t.Save();
    QueryPerformanceCounter(&b);
    LogSwapQueue("snaptest at save");
    {
      // task heap object (sub_82219630 allocates tasks from *(0x828B3A10) via sub_8223D098)
      const uint32_t h = T6Rd32(0x828B3A10u);
      std::string w;
      for (uint32_t k = 0; k < 0x40 && h; k += 4) w += fmt::format(" {:08X}", T6Rd32(h + k));
      REXLOG_INFO("T6 snaptest: task heap object {:08X} page kind {:X}:{}", h, t6_pagemap[h >> 12], w);
    }
    REXLOG_INFO("T6 snaptest: saved {} regions, {} MB in {:.1f} ms at step {}", t.snap.size(), t.bytes >> 20,
                Ms(a, b), s.step);
    t.phase = 1;
    t.i = 0;
  }
  if (t.phase == 2 && t.i == t.frames) {
    REXLOG_INFO("T6 snaptest: done, data/bss differs in {} of {} frames, full hash in {} checks", t.mismatches,
                t.frames, t.full_mismatches);
    T6SndStatsLog();
    t.phase = 3;
  }
  if (t.phase == 1 && t.i == t.frames) {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    LogSwapQueue("snaptest before load");
    t.Load();
    QueryPerformanceCounter(&b);
    REXLOG_INFO("T6 snaptest: restored the state of step {} at step {} in {:.1f} ms", t.at, s.step, Ms(a, b));
    LogSwapQueue("snaptest after load");
    StartHangWatch(&s.step);
    t.phase = 2;
    t.i = 0;
    if (const char* d = T6Env("T6_SNAPDIFF"); d && *d) t.DiffToFile(d, 0);
    // T6_TRACE_AFTER_RESTORE=1: full kernel call trace from here on (find hangs / crashes caused by the restore)
    if (T6Env("T6_TRACE_AFTER_RESTORE")) {
      using TraceFn = void (*)(bool);
      HMODULE mods[] = {GetModuleHandleW(L"rexruntime.dll"), GetModuleHandleW(nullptr)};
      for (HMODULE m : mods) {
        if (auto fn = m ? reinterpret_cast<TraceFn>(GetProcAddress(m, "rex_trace_everything")) : nullptr) {
          fn(true);
          REXLOG_INFO("T6 snaptest: tracing everything from here");
          break;
        }
      }
    }
  }
}

void T6InputHook(PPCRegister& r1) {  if (!g_t6_membase) return;
  t6_thread_bit = 0;
  auto& s = state();
  auto& t = snaptest();
  uint8_t* p1 = g_t6_membase + r1.u32 + kP1Offset;
  uint8_t* p2 = g_t6_membase + r1.u32 + kP2Offset;
  uint8_t* pads = g_t6_membase + kPadLo;
  PracticeEntries(p1, p2);  // (the practice tools: recording / playback of the dummy)

  if (const char* pm = T6Env("T6_PAGEMAP"); pm && *pm) {
    static uint32_t clear_at = 0, dump_at = 0;
    if (!dump_at) {
      const char* st = T6Env("T6_PAGEMAP_STEPS");
      char* e = nullptr;
      clear_at = st ? std::strtoul(st, &e, 0) : 3000;
      dump_at = (st && *e == ',') ? std::strtoul(e + 1, nullptr, 0) : clear_at + 600;
    }
    if (s.step == clear_at) {
      ClearWriteMaps();
      if (T6Env("T6_SNAP_FROM_MAP")) {
        TakeShadow();
        REXLOG_INFO("T6 snaptest: shadow of game memory taken at step {}", s.step);
      }
    }
    if (s.step == dump_at) {
      if (FILE* f = T6Open(pm, "w")) {
        for (uint32_t pg = 0; pg < 0x100000;) {
          const unsigned char k = t6_pagemap[pg];
          uint32_t end = pg + 1;
          while (end < 0x100000 && t6_pagemap[end] == k) end++;
          if (k) std::fprintf(f, "%08X-%08X %5u pages kind %X\n", pg << 12, end << 12, end - pg, k);
          pg = end;
        }
        std::fclose(f);
      }
      // PS3 rb_state rules -> two files:
      //  <file>.main     pages written by the main thread (the snapshot), thread stacks 0x70000000+ left out
      //  <file>.exclude  16-byte runs inside them that keep their live value: written by another thread (PS3
      //                  RB_SNAP_FOREIGN=2) or by the sound library CnuSound2 / XAudio2 (PS3 RB_SOUND_SEPARATE)
      if (FILE* f = T6Open((std::string(pm) + ".main"), "w")) {
        bool first = true;
        for (uint32_t pg = 0; pg < 0x100000;) {
          // game code on any thread (worker results are game state, PS3 RB_SNAP_FOREIGN off), no Direct3D (RSX FIFO rule)
          // physical memory only inside the game heaps (the GPU pools hold textures / command buffers)
          const auto ok = [](uint32_t q) {
            return (t6_pagemap[q] & 0x01) && !(q >= 0x70000 && q < 0x80000) && (q < 0xA0000 || (q >= 0xA7290 && q < 0xABA90));
          };
          if (!ok(pg)) { pg++; continue; }
          uint32_t end = pg + 1;
          while (end < 0x100000 && ok(end)) end++;
          std::fprintf(f, "%s%08X-%08X", first ? "" : ",", pg << 12, end << 12);
          first = false;
          pg = end;
        }
        std::fclose(f);
      }
      if (FILE* f = T6Open((std::string(pm) + ".exclude"), "w")) {
        const auto dump = [&](const unsigned char* fine, size_t n, uint32_t base) {
          for (size_t b = 0; b < n;) {
            // bytes of the sound library (PS3 RB_SOUND_SEPARATE) and of Direct3D (PS3: RSX FIFO blocks)
            if (!(fine[b] & 0x90)) { b++; continue; }
            size_t e = b + 1;
            while (e < n && (fine[e] & 0x90)) e++;
            std::fprintf(f, "%08X-%08X\n", base + static_cast<uint32_t>(b << 4), base + static_cast<uint32_t>(e << 4));
            b = e;
          }
        };
        dump(t6_fine_virt, sizeof(t6_fine_virt), 0x40000000u);
        dump(t6_fine_data, sizeof(t6_fine_data), 0x82726000u);
        dump(t6_fine_heap, sizeof(t6_fine_heap), 0xA7290000u);
        std::fclose(f);
      }
      REXLOG_INFO("T6 netplay: page map of steps {}-{} written to {}", clear_at, dump_at, pm);
    }
  }

  // T6_LOG_MODE=1: changes of the main LCG 0x828B49E0 (PS3: LCG 0x00D142AC nonzero from the fight start = sync point)
  if (T6Env("T6_LOG_MODE")) {
    static uint32_t last = 0, changes = 0, logged = 0, last_change = 0;
    const uint32_t v = T6Rd32(0x828B49E0u);
    if (v != last) {
      // steps of the LCG x = x * 0x0696542D + 12345 since the last frame (bursts while a fight loads)
      uint32_t k = 0, x = last;
      while (x != v && k < 100000) { x = x * 0x0696542Du + 12345u; k++; }
      static uint32_t bursts = 0;
      if (logged < 60 && (changes < 5 || k > 3 || s.step > last_change + 30)) {
        REXLOG_INFO("T6 netplay: step {} LCG {:08X} -> {:08X} (change {}, {} steps){}", s.step, last, v, changes,
                    k == 100000 ? 0u : k, k == 100000 ? " (not a step: reseeded)" : "");
        logged++;
      }
      (void)bursts;
      last = v;
      changes++;
      last_change = s.step;
    }
  }
  // T6_TASKDUMP=1: vtables of the task tree (root *(0x82828C60), links +16..+40 like sub_82219420) when the set changes
  // -> which task types exist only in the fight (start marker of the netplay session)
  if (T6Env("T6_TASKDUMP") && (s.step % 10) == 0) {
    std::vector<uint32_t> todo{T6Rd32(0x82828C60u)};
    std::unordered_map<uint32_t, int> seen;
    std::map<uint32_t, int> vt;
    while (!todo.empty() && seen.size() < 20000) {
      const uint32_t n = todo.back();
      todo.pop_back();
      if (!T6HeapPtr(n) || seen.count(n)) continue;
      seen[n] = 1;
      const uint32_t v = T6Rd32(n);
      if (v >= 0x82000000u && v < 0x82200000u) vt[v]++;
      for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
    }
    static std::map<uint32_t, int> last;
    std::string added, removed;
    // MSVC RTTI: vtable[-1] = complete object locator, +12 type descriptor, +8 decorated name
    const auto rtti = [&](uint32_t v) -> std::string {
      const uint32_t col = T6Rd32(v - 4);
      if (col < 0x82000000u || col >= 0x82DB0000u) return "?";
      const uint32_t td = T6Rd32(col + 12);
      if (td < 0x82000000u || td >= 0x82DB0000u) return "?";
      const char* nm = reinterpret_cast<const char*>(g_t6_membase + td + 8);
      return std::string(nm, strnlen(nm, 80));
    };
    for (const auto& [v, c] : vt) if (!last.count(v)) added += fmt::format(" {:08X}x{}={}", v, c, rtti(v));
    for (const auto& [v, c] : last) if (!vt.count(v)) removed += fmt::format(" {:08X}", v);
    if (!added.empty() || !removed.empty()) {
      REXLOG_INFO("T6 netplay: step {} tasks {} types {}; new:{} gone:{}", s.step, seen.size(), vt.size(), added, removed);
    }
    last = std::move(vt);
  }
  // T6_LOG_MODE=1: CRT rand state of the main thread (_getptd()+20, x = x * 214013 + 2531011) steps per frame
  if (T6Env("T6_LOG_MODE") && g_main_ctx) {
    static uint32_t ptd = 0, last = 0, logged = 0;
    if (!ptd) {
      ptd = T6MainPtd();
      if (ptd) last = T6Rd32(ptd + 20);
    }
    if (ptd) {
      const uint32_t v = T6Rd32(ptd + 20);
      if (v != last) {
        uint32_t k = 0, x = last;
        while (x != v && k < 100000) { x = x * 214013u + 2531011u; k++; }
        if (logged++ < 80) REXLOG_INFO("T6 netplay: step {} CRT rand {:08X} -> {:08X} ({} steps)", s.step, last, v, k);
        last = v;
      }
    }
  }
  // T6_MODEDUMP=<step>: game mode objects (tcb_gamemode_*: vtables 0x82007F84-0x8200BC00 + onlinebattle) at that step,
  // 0x200 bytes each -> log (where the mode keeps the characters / stage for StartMatch)
  if (const char* md = T6Env("T6_MODEDUMP"); md && std::strtoul(md, nullptr, 0) == s.step) {
    std::vector<uint32_t> todo{T6Rd32(0x82828C60u)};
    std::unordered_map<uint32_t, int> seen;
    while (!todo.empty() && seen.size() < 20000) {
      const uint32_t n = todo.back();
      todo.pop_back();
      if (!T6HeapPtr(n) || seen.count(n)) continue;
      seen[n] = 1;
      const uint32_t v = T6Rd32(n);
      if ((v >= 0x82007F84u && v < 0x8200BC00u) || v == 0x82083634u || v == 0x8200C5E8u) {
        for (uint32_t o = 0; o < 0x200; o += 0x40) {
          std::string w;
          for (uint32_t k = 0; k < 0x40; k += 4) w += fmt::format(" {:08X}", T6Rd32(n + o + k));
          REXLOG_INFO("T6 mode {:08X} vt {:08X} +{:03X}:{}", n, v, o, w);
        }
      }
      for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
    }
  }
  // T6_LOG_MODE=1: input manager *(0x8297BB1C) mode +16 / flags +60 changes (fight start marker for the netplay session)
  if (T6Env("T6_LOG_MODE")) {
    static uint32_t last_mode = ~0u, last_flags = ~0u;
    const uint32_t mgr = T6Rd32(0x8297BB1Cu);
    const uint32_t mode = mgr ? T6Rd32(mgr + 16) : 0xFFFFFFFEu, flags = mgr ? T6Rd32(mgr + 60) : 0xFFFFFFFEu;
    if (mode != last_mode || flags != last_flags) {
      REXLOG_INFO("T6 netplay: step {} input manager {:08X} mode {:08X} flags {:08X} P1 write {} read {}", s.step, mgr, mode,
                  flags, mgr ? T6Rd32(mgr + 72) : 0u, mgr ? T6Rd32(mgr + 76) : 0u);
      last_mode = mode;
      last_flags = flags;
    }
  }

  // T6_POKE=<step>:<addr>=<u32>,<step>:<addr>=<u32>,...: write big-endian u32 values at those pad steps (experiments:
  // character / stage selection for StartMatch)
  if (const char* pk = T6Env("T6_POKE"); pk && *pk) {
    for (const char* c = pk; *c;) {
      char* e = nullptr;
      const uint32_t st = std::strtoul(c, &e, 0);
      if (!e || *e != ':') break;
      const uint32_t a = std::strtoul(e + 1, &e, 16);
      if (!e || *e != '=') break;
      const uint32_t v = std::strtoul(e + 1, &e, 0);
      if (st == s.step) {
        *reinterpret_cast<uint32_t*>(g_t6_membase + a) = _byteswap_ulong(v);
        REXLOG_INFO("T6 netplay: poke step {} {:08X} = {}", st, a, v);
      }
      if (!e || *e != ',') break;
      c = e + 1;
    }
  }
  // T6_DUMP_DATA=<step>,<step>,..: XEX data/bss 0x82726000-0x82DB0000 at those pad steps -> runs/data_<step>.bin
  if (const char* dd = T6Env("T6_DUMP_DATA"); dd && *dd) {
    for (const char* c = dd; *c;) {
      char* e = nullptr;
      const uint32_t st = std::strtoul(c, &e, 0);
      if (st == s.step) {
        if (FILE* f = T6Open(T6DumpPath(fmt::format("data_{}.bin", st)), "wb")) {
          std::fwrite(g_t6_membase + 0x82726000u, 1, 0x82DB0000u - 0x82726000u, f);
          std::fclose(f);
        }
        if (FILE* f = T6Open(T6DumpPath(fmt::format("heap_{}.bin", st)), "wb")) {
          std::fwrite(g_t6_membase + 0xA7290000u, 1, 0xABA90000u - 0xA7290000u, f);
          std::fclose(f);
        }
      }
      if (!e || *e != ',') break;
      c = e + 1;
    }
  }
  // T6_DUMP_IMAGE=<file>: write the loaded XEX image (0x82000000-0x82DB0000) once, for string / data analysis
  if (s.step == 60) {
    if (const char* d = T6Env("T6_DUMP_IMAGE"); d && *d) {
      if (FILE* f = T6Open(d, "wb")) {
        std::fwrite(g_t6_membase + 0x82000000u, 1, 0xDB0000u, f);
        std::fclose(f);
        REXLOG_INFO("T6 netplay: image dumped to {}", d);
      }
    }
  }

  // Replay from file (paused during the second pass of the snapshot test)
  // T6_PLAY_UNTIL=<step>: the replay stops there (the rest is driven by T6_AUTO_CONFIRM / the controllers)
  if (s.play && !s.play_done) {
    static const uint32_t until = T6EnvU32("T6_PLAY_UNTIL", ~0u, 0);
    if (s.step >= until) {
      s.play_done = true;
      REXLOG_INFO("T6 netplay: replay stopped at step {} (T6_PLAY_UNTIL)", s.step);
    }
  }
  if (s.play && !s.play_done && t.phase != 2) {
    uint32_t step = 0;
    static std::vector<uint8_t> buf(kRecSize);
    if (std::fread(&step, 4, 1, s.play) == 1 && std::fread(buf.data(), kRecSize, 1, s.play) == 1) {
      std::memcpy(p1, buf.data(), kEntrySize);
      std::memcpy(p2, buf.data() + kEntrySize, kEntrySize);
      std::memcpy(pads, buf.data() + 2 * kEntrySize, kPadHi - kPadLo);
    } else {
      s.play_done = true;
      REXLOG_INFO("T6 netplay: replay ended at step {}", s.step);
    }
  }

  // T6_SNAPDUMP=<frame>,<file prefix>: the saved ranges at that frame of both passes -> <prefix>1.bin / <prefix>2.bin
  // (u32 addr, u32 size, bytes per range) for tools/snapdump_diff.py
  if (const char* d = T6Env("T6_SNAPDUMP"); d && *d && (t.phase == 1 || t.phase == 2)) {
    char* e = nullptr;
    const uint32_t k = std::strtoul(d, &e, 0);
    if (t.i == k && e && *e == ',') {
      if (FILE* f = T6Open((std::string(e + 1) + (t.phase == 1 ? "1.bin" : "2.bin")), "wb")) {
        for (const auto& r : SnapRanges()) {
          ForEachCommitted(r.lo, r.hi, [&](uint32_t a, uint32_t n) {
            std::fwrite(&a, 4, 1, f);
            std::fwrite(&n, 4, 1, f);
            std::fwrite(g_t6_membase + a, 1, n, f);
          });
        }
        std::fclose(f);
      }
    }
  }
  if (t.phase == 1) {
    std::vector<uint8_t> in(kRecSize);
    std::memcpy(in.data(), p1, kEntrySize);
    std::memcpy(in.data() + kEntrySize, p2, kEntrySize);
    std::memcpy(in.data() + 2 * kEntrySize, pads, kPadHi - kPadLo);
    t.inputs.push_back(std::move(in));
    t.first.push_back(s.Hashes());
    t.first_full.push_back(t.i % 30 == 0 ? FullHashes() : std::vector<uint64_t>{});
    t.i++;
  } else if (t.phase == 2) {
    const auto& in = t.inputs[t.i];
    std::memcpy(p1, in.data(), kEntrySize);
    std::memcpy(p2, in.data() + kEntrySize, kEntrySize);
    std::memcpy(pads, in.data() + 2 * kEntrySize, kPadHi - kPadLo);
    const auto h = s.Hashes();
    if (h != t.first[t.i] && t.mismatches++ < 20) {
      REXLOG_INFO("T6 snaptest: frame {} differs in the data/bss hash", t.i);
    }
    if (t.i % 30 == 0) {
      const auto f = FullHashes();
      std::string which;
      for (size_t k = 0; k < f.size(); k++) {
        if (f[k] != t.first_full[t.i][k]) which += fmt::format(" {:08X}-{:08X}", SnapRanges()[k].lo, SnapRanges()[k].hi);
      }
      if (!which.empty()) t.full_mismatches++;
      REXLOG_INFO("T6 snaptest: frame {} full hash {}", t.i, which.empty() ? std::string("equal") : "differs in" + which);
    }
    // The re-simulation ends at the next frame boundary (T6FrameHook), not here: the rest of this frame is still
    // re-simulated, so its sound calls are replayed, not run a second time (PS3: the last re-simulated frame played
    // its sounds twice -> here a voice released twice, XAudio crash 824EED98 right after the test, st58)
    ++t.i;
  }

  if (s.rec) {
    std::fwrite(&s.step, 4, 1, s.rec);
    std::fwrite(p1, kEntrySize, 1, s.rec);
    std::fwrite(p2, kEntrySize, 1, s.rec);
    std::fwrite(pads, kPadHi - kPadLo, 1, s.rec);
    if ((s.step & 63) == 0) std::fflush(s.rec);
  }
  if (s.hash) {
    std::fprintf(s.hash, "%u", s.step);
    for (const uint64_t h : s.Hashes()) std::fprintf(s.hash, " %016llx", static_cast<unsigned long long>(h));
    std::fputc('\n', s.hash);
    if ((s.step & 63) == 0) std::fflush(s.hash);
  }
  s.step++;
}

// Fight input read (PS3 FUN_00185cc8): the input manager entries into fight pads 9/10, called only in a fight.
// Overrides the weak generated symbol. g_fight_reads: calls so far (fight start marker of the netplay session).
extern "C" void __imp__sub_82267058(PPCContext& ctx, uint8_t* base);
uint64_t g_fight_reads = 0;
uint32_t g_fight_last_step = 0;
extern "C" void sub_82267058(PPCContext& ctx, uint8_t* base) {
  const uint32_t step = state().step;
  if (T6Env("T6_LOG_MODE") && (g_fight_reads == 0 || step > g_fight_last_step + 1)) {
    REXLOG_INFO("T6 netplay: fight input read starts at step {} (previous read at step {})", step, g_fight_last_step);
  }
  g_fight_reads++;
  g_fight_last_step = step;
  __imp__sub_82267058(ctx, base);
}
