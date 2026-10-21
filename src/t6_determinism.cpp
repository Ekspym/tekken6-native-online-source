// Tekken 6 netplay - random numbers, seeds, virtual time base and tick count (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

extern "C" void sub_82525280(PPCContext& ctx, uint8_t* base) {
  const uint32_t q = ctx.r3.u32;
  int i = 0;
  while (i < 4 && g_file_queues[i] && g_file_queues[i] != q) i++;
  if (i < 4 && !g_file_queues[i]) {
    g_file_queues[i] = q;
    std::string h;
    for (uint32_t o = 0; o < 0x2900; o += 4) {
      const uint32_t v = T6Rd32(q + o);
      if ((v >> 24) == 0xF8) h += fmt::format(" +{:X}={:08X}", o, v);
    }
    REXLOG_INFO("T6 net: file request queue {} = {:08X} (thread {}), handles:{}", i, q, t6_thread_bit ? "other" : "main", h);
  }
  g_file_requests++;
  __imp__sub_82525280(ctx, base);
}

// CRT rand() (sub_826A0DA8, state _getptd()+20 per thread). T6_RAND_LOG=1: callers (LR) per thread kind, logged at the
// first frames of the session -> which random numbers are drawn on other threads (nondeterministic order)
extern "C" void __imp__sub_826A0DA8(PPCContext& ctx, uint8_t* base);
std::mutex g_rand_mutex;
std::map<std::pair<int, uint32_t>, uint64_t> g_rand_callers;
// Cosmetic callers draw a varying number of values per frame (m1/m2: stage crowd tcb_kkMob* 821BA884 / 821B86B4, then
// particles ZSGEfctParticleATcb 82258324) and pulled the main thread's rand state, which the fight logic uses too, apart
// between the instances. PS3 did the same: cosmetic tasks run with their own rand state (host side, not rolled back).
uint32_t T6NetFrameNo();
bool CosmeticRandCaller(uint32_t lr) {
  return (lr >= 0x821B0000u && lr < 0x821C0000u) || lr == 0x82258324u;
}
extern "C" void sub_826A0DA8(PPCContext& ctx, uint8_t* base) {
  static const bool log = T6Env("T6_RAND_LOG") != nullptr;
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if (log) {
    std::lock_guard lock(g_rand_mutex);
    g_rand_callers[{t6_thread_bit ? 1 : 0, lr}]++;
  }
  static const bool shared = T6Env("T6_COSMETIC_RAND_SHARED") != nullptr;
  if (!shared && t6_thread_bit == 0 && CosmeticRandCaller(lr) && g_main_ctx) {
    PPCContext tmp = ctx;
    __imp__sub_826A5ED0(tmp, base);
    if (const uint32_t ptd = tmp.r3.u32) {
      static uint32_t cosmetic = _byteswap_ulong(12345u);
      uint32_t* st = reinterpret_cast<uint32_t*>(base + ptd + 20);
      const uint32_t game = *st;
      *st = cosmetic;
      __imp__sub_826A0DA8(ctx, base);
      cosmetic = *st;
      *st = game;
      // T6_COSMETIC_TRACE=<from>,<to>: every cosmetic rand of those session frames -> <dump dir>/cosmetic.txt
      if (static const char* tr = T6Env("T6_COSMETIC_TRACE"); tr && T6NetRunning()) {
        static const uint32_t from = std::strtoul(tr, nullptr, 0);
        static const uint32_t to = std::strchr(tr, ',') ? std::strtoul(std::strchr(tr, ',') + 1, nullptr, 0) : from;
        static FILE* f = [] { const char* d = T6Env("T6_NET_DUMP_DIR"); return T6Open(fmt::format("{}/cosmetic.txt", d ? d : "."), "w"); }();
        const uint32_t fr = T6NetFrameNo();
        if (f && fr >= from && fr <= to) {
          std::fprintf(f, "%u lr %08X value %u r30 %08X r31 %08X\n", fr, lr, ctx.r3.u32, ctx.r30.u32, ctx.r31.u32);
          std::fflush(f);
        }
      }
      return;
    }
  }
  __imp__sub_826A0DA8(ctx, base);
}
uint64_t T6SndResultHash();
uint64_t T6SndResultCalls();
extern std::mutex g_tb_mutex;
extern std::map<uint32_t, uint64_t> g_tb_reads;
void T6RandLog(uint32_t frame) {
  if (!T6Env("T6_RAND_LOG")) return;
  std::lock_guard lock(g_rand_mutex);
  std::string m, o;
  for (const auto& [k, n] : g_rand_callers) (k.first ? o : m) += fmt::format(" {:08X}x{}", k.second, n);
  uint32_t crt = 0;
  if (const uint32_t ptd = T6MainPtd()) crt = T6Rd32(ptd + 20);
  REXLOG_INFO("T6 net: frame {} sound results {:016X} ({} calls) LCG {:08X} CRT {:08X}; rand() callers main:{} | other threads:{}", frame,
              T6SndResultHash(), T6SndResultCalls(),
              T6Rd32(0x828B49E0u), crt, m, o);
  g_rand_callers.clear();
  std::string tb;
  {
    std::lock_guard tl(g_tb_mutex);
    for (auto& [t, n] : g_tb_reads) {
      if (n > 1) tb += fmt::format(" {}x{}", t, n - 1);
      n = 1;  // keep the entry: its thread is logged once
    }
  }
  if (!tb.empty()) REXLOG_INFO("T6 net: frame {} time base reads of other threads:{}", frame, tb);
}

// srand of the main LCG (0x828B49E0). T6_FIX_SEED=<n> replaces the seed (replays; netplay will use the agreed seed).
void T6SeedHook(PPCRegister& r3) {
  static const char* fix = T6Env("T6_FIX_SEED");
  static uint32_t calls = 0;
  const uint32_t seed = r3.u32;
  if (fix && *fix) r3.u64 = std::strtoul(fix, nullptr, 0);
  if (calls++ < 64) {
    REXLOG_INFO("T6 netplay: srand(0x{:08X}) -> 0x{:08X} at pad step {}", seed, r3.u32, state().step);
  }
}

// CRT srand (srand(time(NULL)) at boot): random character / stage select, Arcade opponents. Same T6_FIX_SEED.
void T6CrtSeedHook(PPCRegister& r3) {
  static const char* fix = T6Env("T6_FIX_SEED");
  const uint32_t seed = r3.u32;
  if (fix && *fix) r3.u64 = std::strtoul(fix, nullptr, 0);
  REXLOG_INFO("T6 netplay: CRT srand(0x{:08X}) -> 0x{:08X} at pad step {}", seed, r3.u32, state().step);
}


// ---- Virtual time base of the main thread (PS3 RB_VIRTUAL_TB) ----
// The game reads the time base (mftb -> REX_QUERY_TIMEBASE, redirected to t6_timebase by tools/pagemap_patch.py).
// PS3: slow frames (40 ms, no rollback) made the simulation diverge, so the main thread gets time from the frame
// number: frame x 1/60 s + 10 us per read in the frame. A re-simulated frame reads the same time as the live one.
// Other threads (render, sound, workers) keep the real time. T6_VIRTUAL_TB=1.
std::mutex g_tb_mutex;
std::map<uint32_t, uint64_t> g_tb_reads;  // host thread id -> time base reads (other threads than main, in a session)
thread_local bool g_tb_real = false;  // this read takes the host clock (frame timer test, T6_TB_FRAMETIMER_REAL)
extern "C" uint64_t t6_timebase() {
  static const bool on = T6Env("T6_VIRTUAL_TB") != nullptr;
  if (g_tb_real) return rex::chrono::Clock::QueryGuestTickCount();
  // The guest audio callback (rex audio worker) reads the time once per call: in the audio lockstep it gets the time of
  // its callback number (256 samples at 48 kHz each), so the sound library's elapsed-time math is equal on both
  // instances (fade / stream ramps went apart by the host clock: an intro sound "finished" a frame apart, frame ~231).
  if (on && t6_thread_bit != 0 && net_config().enabled) {
    const auto cb = T6Rex().audio_worker_callback;
    uint64_t n = 0;
    if (cb && cb(&n)) {
      static const uint64_t freq = rex::chrono::Clock::guest_tick_frequency();
      thread_local uint64_t last_n = ~0ull, reads = 0;
      if (n != last_n) {
        last_n = n;
        reads = 0;
      }
      return (1ull << 41) + n * freq * 256 / 48000 + (++reads) * freq / 1000000;
    }
  }
  // T6_TB_THREADS=1 (diagnostics): which guest threads read the time base, how often (logged with T6_TB_STATS)
  if (static const bool tbt = T6Env("T6_TB_THREADS") != nullptr; tbt && t6_thread_bit != 0) {
    std::lock_guard lock(g_tb_mutex);
    auto& n = g_tb_reads[GetCurrentThreadId()];
    if (n++ == 0) {
      auto* xt = rex::system::XThread::GetCurrentThread();
      REXLOG_INFO("T6 time base: read by thread {}: guest start {:08X} name '{}'", GetCurrentThreadId(),
                  xt ? xt->creation_params()->start_address : 0, xt ? xt->name() : std::string("host"));
    }
  }
  if (!on || t6_thread_bit != 0) {
    // (read once, like the switches above: the GPU vblank thread reads the time base at every vblank, and the parameter
    // lookup there crashed 5 games in a match - honk, lax, lmsSOL, Yuii 2026-10-18..20, host addr 0x88 in T6Env's map find)
    static const bool rand_log = T6Env("T6_RAND_LOG") != nullptr;
    if (rand_log && T6NetRunning()) {  // diagnostics: who else reads the time base in a session
      std::lock_guard lock(g_tb_mutex);
      auto& n = g_tb_reads[GetCurrentThreadId()];
      if (n++ == 0) {
        auto* xt = rex::system::XThread::GetCurrentThread();
        REXLOG_INFO("T6 net: time base read by thread {}: guest start {:08X} context {:08X} name '{}'", GetCurrentThreadId(),
                    xt ? xt->creation_params()->start_address : 0, xt ? xt->creation_params()->start_context : 0,
                    xt ? xt->name() : std::string("host"));
      }
    }
    return rex::chrono::Clock::QueryGuestTickCount();
  }
  static const uint64_t freq = rex::chrono::Clock::guest_tick_frequency();
  static const uint64_t base = rex::chrono::Clock::QueryGuestTickCount();
  static uint32_t last_frame = ~0u;
  static uint64_t reads = 0;
  const uint32_t frame = T6SimFrame();
  if (frame != last_frame) {
    // T6_TB_STATS=1 (diagnostics): most time base reads of the main thread in one frame, every 600 frames
    if (static const bool stats = T6Env("T6_TB_STATS") != nullptr; stats) {
      static uint64_t most = 0;
      static uint32_t n = 0;
      most = std::max(most, reads);
      if (++n % 600 == 0) {
        REXLOG_INFO("T6 time base: at most {} main thread reads in a frame (frame {})", most, frame);
        std::lock_guard lock(g_tb_mutex);
        for (const auto& [tid, cnt] : g_tb_reads) REXLOG_INFO("T6 time base:   thread {} {} reads", tid, cnt);
        most = 0;
      }
    }
    last_frame = frame;
    reads = 0;
  }
  // In a netplay session both instances read the same time: a fixed base far after anything read before the session
  if (T6NetRunning()) return (1ull << 40) + frame * freq / 60 + (++reads) * freq / 100000;
  return base + frame * freq / 60 + (++reads) * freq / 100000;
}

// Frame timer of the main loop (sub_82219B70): sub_82516080 stores QueryPerformanceCounter in a thread record,
// sub_825160B8 returns the time since then (the particle effects' time step). T6_TB_FRAMETIMER_REAL=1 (test): both read
// the host clock.
extern "C" void __imp__sub_82516080(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_825160B8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82516080(PPCContext& ctx, uint8_t* base) {
  static const bool real = T6Env("T6_TB_FRAMETIMER_REAL") != nullptr;
  if (T6Env("T6_TB_FTLOG")) {
    static uint32_t n = 0;
    if (n++ % 120 < 4) REXLOG_INFO("T6 frame timer: start lr {:08X} step {}", static_cast<uint32_t>(ctx.lr), state().step);
  }
  g_tb_real = real;
  __imp__sub_82516080(ctx, base);
  g_tb_real = false;
}
extern "C" void sub_825160B8(PPCContext& ctx, uint8_t* base) {
  static const bool real = T6Env("T6_TB_FRAMETIMER_REAL") != nullptr;
  g_tb_real = real;
  __imp__sub_825160B8(ctx, base);
  g_tb_real = false;
  // With the virtual time base this elapsed time was only the few 10 us steps of the reads in between: the particle
  // effects advance by it (ki charge aura, EWGF lightning stayed at size 0, user 2026-10-12). One frame (1/60 s) on
  // both instances = what the game gets at 60 fps, deterministic.
  static const bool vtb = T6Env("T6_VIRTUAL_TB") != nullptr;
  if (T6Env("T6_TB_FTLOG")) {  // diagnostics: what the frame timer returns, by caller
    static uint32_t n = 0;
    if (n++ % 120 < 4)
      REXLOG_INFO("T6 frame timer: {} ticks ({:.3f} ms) lr {:08X} step {}", ctx.r3.u64,
                  ctx.r3.u64 * 1000.0 / rex::chrono::Clock::guest_tick_frequency(), static_cast<uint32_t>(ctx.lr), state().step);
  }
  // Two calls per frame (sub_82219B70): lr 82219BA4 = the frame's work so far (host: 5-7 ms, a load figure) and
  // lr 82219C68 = the whole frame with the vsync wait (16.7 ms, the effects' time step). A full frame for both made the
  // load 100 % and the game left the effects out; the virtual 10 us steps made the time step 0. Fixed, the same on
  // both instances: 6 ms of work, 1/60 s per frame.
  if (vtb && !real) {
    const uint64_t f = rex::chrono::Clock::guest_tick_frequency();
    ctx.r3.u64 = static_cast<uint32_t>(ctx.lr) == 0x82219BA4u ? f * 6 / 1000 : f / 60;
  }
}

// ---- Virtual tick count / system time (T6_NET) ----
// GetTickCount of the game (sub_824BDC70: KeTimeStampBundle->TickCount, read straight from kernel memory, 59 callers) and
// GetSystemTimeAsFileTime (sub_824BDC80 -> KeQuerySystemTime) returned the real clock: each instance saw another time,
// so whatever the game picks by the clock differed (a character's intro 1 on one screen, intro 2 on the other, while
// LCG / CRT rand / sound results were equal). In a netplay run both come from the simulation frame, on every thread.
extern "C" void __imp__sub_824BDC70(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_824BDC80(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824BDC70(PPCContext& ctx, uint8_t* base) {
  static const bool on = T6Env("T6_NET") != nullptr && T6Env("T6_REAL_CLOCK") == nullptr;
  if (!on) {
    __imp__sub_824BDC70(ctx, base);
    return;
  }
  // + 1 ms per 16 reads of this thread within the frame, so a loop that waits on the tick count still ends
  thread_local uint32_t last_frame = ~0u, reads = 0;
  const uint32_t frame = T6SimFrame();
  if (frame != last_frame) {
    last_frame = frame;
    reads = 0;
  }
  ctx.r3.u64 = static_cast<uint32_t>(600000u + static_cast<uint64_t>(frame) * 1000 / 60 + (reads++ >> 4));
}
extern "C" void sub_824BDC80(PPCContext& ctx, uint8_t* base) {
  static const bool on = T6Env("T6_NET") != nullptr && T6Env("T6_REAL_CLOCK") == nullptr;
  if (!on || !ctx.r3.u32) {
    __imp__sub_824BDC80(ctx, base);
    return;
  }
  // FILETIME (100 ns units), big endian: a fixed date + 1/60 s per simulation frame
  const uint64_t t = 0x01DB000000000000ull + static_cast<uint64_t>(T6SimFrame()) * 166667ull;
  *reinterpret_cast<uint64_t*>(base + ctx.r3.u32) = _byteswap_uint64(t);
}


// ---- Third RNG (sub_822988A0, state 0x828D9F50) diagnostics ----
// The fight / intro code draws from it (character positions and camera of the round intro, frame 208); its values were
// apart between two instances from session frame 52 on while the LCG and the CRT rand were equal. T6_RAND_LOG: per pad
// step the state before the first call and the callers (lr x count, * = another thread) -> <dump dir>/rng3.txt.
extern "C" void __imp__sub_822988A0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822988A0(PPCContext& ctx, uint8_t* base) {
  static const bool log = T6Env("T6_RAND_LOG") != nullptr && T6Env("T6_NET_DUMP_DIR") != nullptr;
  if (log) {
    static std::mutex m;
    static FILE* f = T6Open(fmt::format("{}/rng3.txt", T6Env("T6_NET_DUMP_DIR")), "w");
    static uint32_t cur_step = ~0u, first_state = 0;
    static std::map<std::pair<int, uint32_t>, uint32_t> callers;
    std::lock_guard lock(m);
    const uint32_t step = state().step;
    if (step != cur_step) {
      if (f && !callers.empty()) {
        std::string s;
        for (const auto& [k, n] : callers) s += fmt::format(" {}{:08X}x{}", k.first ? "*" : "", k.second, n);
        std::fprintf(f, "step %u frame %u state %08X:%s\n", cur_step, T6NetRunning() ? T6NetFrameNo() : 0, first_state, s.c_str());
        std::fflush(f);
      }
      callers.clear();
      cur_step = step;
      first_state = T6Rd32(0x828D9F50u);
    }
    callers[{t6_thread_bit ? 1 : 0, static_cast<uint32_t>(ctx.lr)}]++;
  }
  __imp__sub_822988A0(ctx, base);
}

// Master seed of the third RNG (object 0x828D9E48: +0 master seed, +4 table of 64 seeds, +260 index, +264 state).
// sub_82298BB8 (called by the game mode inits) hashes the local time string with seconds into it; the battle init builds
// the seed table from it and every mode change reseeds the state from the table. Two instances hashed two wall clock
// seconds: other positions / camera of the round intro on each screen (state apart from session frame 52, intro at
// 208). The game's own online setups store 0 instead (sub_82298858(0)); in a netplay run it is the agreed seed.
extern "C" void __imp__sub_82298BB8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82298BB8(PPCContext& ctx, uint8_t* base) {
  if (!net_config().enabled) {
    __imp__sub_82298BB8(ctx, base);
    return;
  }
  const uint32_t seed = net_config().seed;
  *reinterpret_cast<uint32_t*>(base + 0x828D9E48u) = _byteswap_ulong(seed);
  REXLOG_INFO("T6 net: third RNG master seed {} (clock hash replaced, lr {:08X})", seed, static_cast<uint32_t>(ctx.lr));
}
