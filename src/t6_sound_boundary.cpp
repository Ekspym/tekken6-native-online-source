// Tekken 6 netplay - sound boundary (sound calls replayed / recorded across rollbacks) (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Sound boundary (port of the PS3 hybrid's t6n_snd_call / RB_SOUND_SEPARATE; tools/sndcall_patch.py) ----
// The sound library (MMC, CnuSound2, XAudio2) is not rolled back. Every call of the game into it (outermost only) is
// recorded in live frames: function, call site, occurrence in the frame, result r3/f1 and the bytes the library wrote
// into memory that a snapshot load restores. In a re-simulated frame the same call (frame, function, call site,
// occurrence) is not run again: its result and writes are replayed. Unmatched calls run and are recorded.
extern "C" constinit thread_local int t6_snd_capture = 0;
namespace t6np {
struct SndRec {
  uint32_t frame, addr, lr, nth;
  bool used;
  uint64_t in[4];  // r3..r6 at the call (T6_SND_DIAG)
  uint64_t r3;
  double f1;
  std::vector<uint8_t> data;
  std::vector<uint8_t> before;  // the same bytes before the call wrote them (the replay checks them)
  std::vector<std::pair<uint32_t, uint32_t>> writes;
};
std::vector<std::pair<uint32_t, uint32_t>> g_cap;
struct CapOld {
  uint32_t a, n;
  uint8_t old[16];
};
std::vector<CapOld> g_cap_old;  // every kept store in the order it ran, with the bytes it replaced
uint64_t g_snd_refused[2] = {};  // replay: groups of writes refused / applied
std::unordered_map<uint32_t, std::vector<SndRec>> g_snd_recs;  // by frame
size_t g_snd_count = 0;
int g_snd_depth = 0;
uint32_t g_snd_cur_addr = 0, g_snd_cur_lr = 0;  // the recorded call that is running (voice diagnostics)
uint64_t g_snd_stats[4] = {};  // live, replayed, unmatched, writes replayed
uint64_t g_snd_dev = 0;  // device updates replayed in re-simulated frames
uint64_t g_snd_result_hash = 0xcbf29ce484222325ull, g_snd_result_calls = 0;
}  // namespace t6np
double g_snd_ms[3] = {};
uint64_t g_snd_caps[2] = {};

// per log window: the value is reset when read
uint64_t T6SndResultHash() { const uint64_t h = g_snd_result_hash; g_snd_result_hash = 0xcbf29ce484222325ull; return h; }
uint64_t T6SndResultCalls() { const uint64_t n = g_snd_result_calls; g_snd_result_calls = 0; return n; }
// new session: the records are keyed by the session frame
void T6SndReset() {
  g_snd_recs.clear();
  g_snd_count = 0;
}
void T6SndStatsLog() {
  REXLOG_INFO("T6 sound boundary: {} live, {} replayed ({} writes), {} unmatched in re-simulation", g_snd_stats[0],
              g_snd_stats[1], g_snd_stats[3], g_snd_stats[2]);
}

// Every store of a recorded library call comes here: ~47500 per frame (rb1), almost all into the library's own heap,
// its statics or the stack, which the record drops anyway - collecting and sorting them cost 1.3 ms per live frame.
// Only stores into memory a snapshot load restores are kept: XEX data/bss, the game heaps, the game's pages of the
// physical range.
extern "C" void t6_cap(uint32_t a, uint32_t n) {
  if (a - 0x82726000u < 0x68A000u || a - 0xA7290000u < 0x4800000u || a - 0x40000000u < 0x10000000u) {
    g_cap.emplace_back(a, n);
    CapOld& o = g_cap_old.emplace_back();
    o.a = a;
    o.n = n <= 16 ? n : 16;
    std::memcpy(o.old, g_t6_membase + a, o.n);  // called before the store (T6_CAP in REX_STORE_*)
  }
}

// The recorded writes of a library call put back into the game memory of a re-simulated frame. The library wrote
// into objects of the GAME heap (voice entries, request results). In the corrected frames such an address can belong
// to another object: the discarded frame asked for a sound, the corrected one does not, and the heap gave the block
// to something else. A blind replay then overwrote that object - 2026-10-13, v2.1.0, the user's match: "Call to
// invalid or unregistered function at guest address 0x00000552 (lr 822188F8 ...)", a state function pointer at +48
// of a game object replaced by a small number. So a write is only replayed where the memory still holds what the
// call found there when it ran live (or already what it wrote); writes close together (one object) go together or
// not at all.
static void SndReplayWrites(uint8_t* base, const SndRec& r) {
  const size_t count = r.writes.size();
  size_t at = 0;
  for (size_t i = 0; i < count;) {
    // a group: ranges less than 128 bytes apart
    size_t j = i + 1;
    while (j < count && r.writes[j].first - (r.writes[j - 1].first + r.writes[j - 1].second) < 128) j++;
    bool ok = r.before.size() == r.data.size();
    size_t p = at;
    for (size_t k = i; k < j && ok; k++) {
      const auto& [a, n] = r.writes[k];
      ok = PageReadable(a >> 12) && PageReadable((a + n - 1) >> 12) &&
           (!std::memcmp(base + a, r.before.data() + p, n) || !std::memcmp(base + a, r.data.data() + p, n));
      p += n;
    }
    p = at;
    for (size_t k = i; k < j; k++) {
      const auto& [a, n] = r.writes[k];
      if (ok) std::memcpy(base + a, r.data.data() + p, n);
      p += n;
    }
    g_snd_refused[ok ? 1 : 0]++;
    if (!ok) {
      static int logged = 0;
      if (logged < 40) {
        logged++;
        REXLOG_INFO("T6 sound boundary: replay of {:08X} (lr {:08X}) frame {}: writes at {:08X}.. refused - the memory there is not what the call found",
                    r.addr, r.lr, r.frame, r.writes[i].first);
      }
    }
    at = p;
    i = j;
  }
}

uint32_t T6NetFrameNo();
uint64_t T6NetLoads();
bool T6Resimulating() { return T6NetRunning() ? T6NetResim() : snaptest().phase == 2; }
// Frame number of the simulation: the snapshot test re-simulates steps at..; a netplay session counts its own frames
// (both instances agree on them, the pad steps before the session differ)
uint32_t T6SimFrame() {
  if (T6NetRunning()) return 1000000u + T6NetFrameNo();
  return snaptest().phase == 2 ? snaptest().at + snaptest().i : state().step;
}

extern "C" void t6_snd_call(PPCContext& ctx, uint8_t* base, uint32_t addr, PPCFunc* fn) {
  static const bool on = T6Env("T6_SOUND_SEPARATE") != nullptr;
  // T6_SND_LOG=1 (finding a sound's id, e.g. the "new challenger" for the launcher's match found, 2026-10-14): every
  // new combination of function, call site and arguments r4..r6 of the game thread's calls into the library, once
  if (static const bool log = T6Env("T6_SND_LOG") != nullptr; log && g_snd_depth == 0 && t6_thread_bit == 0) {
    static std::unordered_set<uint64_t> seen;
    uint64_t h = 0xcbf29ce484222325ull;
    for (const uint64_t v : {static_cast<uint64_t>(addr), static_cast<uint64_t>(ctx.lr), ctx.r4.u64, ctx.r5.u64, ctx.r6.u64})
      h = (h ^ v) * 0x100000001b3ull;
    if (seen.insert(h).second)
      REXLOG_INFO("T6 snd call {:08X} from {:08X} step {}: r3 {:X} r4 {:X} r5 {:X} r6 {:X}", addr, static_cast<uint32_t>(ctx.lr),
                  state().step, ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
  }
  // Only the main (game) thread is rolled back; the render, worker and sound threads call the library directly
  // (PS3: sound_frame "active" only on the main PPU thread)
  if (!on || g_snd_depth > 0 || t6_thread_bit != 0) {
    fn(ctx, base);
    return;
  }
  T6AudioJoin();  // a step started in the draw runs to its end before the game calls the library again (same order on both sides)
  const double snd_t0 = T6NowMs();
  struct SndTime {
    double t0, fn = 0; bool resim = false, replayed = false;
    ~SndTime() {
      const double all = T6NowMs() - t0;
      if (replayed) { g_snd_ms[2] += all; return; }
      g_snd_ms[0] += fn;
      g_snd_ms[1] += all - fn;
    }
  } snd_time{snd_t0};
  const uint32_t frame = T6SimFrame();
  const bool resim = T6Resimulating();
  static uint32_t last_frame = ~0u;
  static int last_phase = -1;
  static uint64_t last_loads = 0;
  static std::unordered_map<uint64_t, uint32_t> site_count;
  // PS3: a new pass = another frame OR another snapshot load (a 1-frame rollback re-simulates the same frame number)
  if (frame != last_frame || snaptest().phase != last_phase || T6NetLoads() != last_loads) {
    last_frame = frame;
    last_phase = snaptest().phase;
    last_loads = T6NetLoads();
    site_count.clear();
    // keep the last 600 frames (rollbacks reach at most a few frames back; the snapshot test re-runs 120)
    for (auto it = g_snd_recs.begin(); it != g_snd_recs.end();) {
      if (it->first + 600 < frame || it->first > frame + 600) {
        g_snd_count -= it->second.size();
        it = g_snd_recs.erase(it);
      } else {
        ++it;
      }
    }
    if (auto it = g_snd_recs.find(frame); it != g_snd_recs.end()) {
      for (auto& r : it->second) r.used = false;
    }
  }
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  const uint32_t nth = site_count[(static_cast<uint64_t>(addr) << 32) | lr]++;
  const uint64_t in[4] = {ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64};
  if (resim) {
    const auto replay = [&](SndRec& r) {
      r.used = true;
      ctx.r3.u64 = r.r3;
      ctx.f1.f64 = r.f1;
      // (the cached answer of PageReadable: a VirtualQuery per replayed write cost ~1.2 ms per re-simulated
      // frame, rb3 - 32 calls x ~5 writes)
      SndReplayWrites(base, r);
      g_snd_stats[1]++;
      g_snd_stats[3] += r.writes.size();
      snd_time.replayed = true;
    };
    for (auto& r : g_snd_recs[frame]) {
      if (!r.used && r.addr == addr && r.lr == lr && r.nth == nth) {
        replay(r);
        return;
      }
    }
    // (Tried 2026-10-13: a call without a record taking the unused record of the SAME request - function, call site,
    // r3..r6 - of this frame or the one before / after. It never matched in 30000 replays (vo3): when a voice of a
    // discarded frame ends as an orphan, the corrected frames do not make that request at all (T6_SND_DIAG) = a sound
    // of the wrong prediction, which is rightly cut.)
    g_snd_stats[2]++;
    // T6_SND_DIAG=1: every call of a re-simulated frame that runs for real, and the records of the frames around it
    // that the re-simulation did not use (same function) - why a moved request was not recognized
    if (static const bool diag = T6Env("T6_SND_DIAG") != nullptr; diag && T6NetRunning()) {
      std::string free_recs;
      for (const uint32_t f : {frame - 1, frame, frame + 1}) {
        const auto it = g_snd_recs.find(f);
        if (it == g_snd_recs.end()) continue;
        for (const auto& r : it->second) {
          if (r.addr == addr && !(f <= frame && r.used)) {
            free_recs += fmt::format(" [frame {:+d} lr {:08X} nth {} in {:X} {:X} {:X} {:X}{}]", static_cast<int>(f - frame), r.lr, r.nth,
                                     r.in[0], r.in[1], r.in[2], r.in[3], f > frame ? " (not re-simulated yet)" : "");
          }
        }
      }
      REXLOG_INFO("T6 snd diag: frame {} unmatched {:08X} lr {:08X} nth {} in {:X} {:X} {:X} {:X}; free records:{}", T6NetFrameNo(), addr, lr, nth,
                  in[0], in[1], in[2], in[3], free_recs);
    }
  } else {
    g_snd_stats[0]++;
  }
  auto& recs = g_snd_recs[frame];
  g_snd_count -= std::erase_if(recs, [&](const SndRec& r) { return r.addr == addr && r.lr == lr && r.nth == nth; });
  SndRec rec{frame, addr, lr, nth, true, {in[0], in[1], in[2], in[3]}, 0, 0.0, {}, {}};
  const uint32_t sp = ctx.r1.u32;
  g_cap.clear();
  g_cap_old.clear();
  g_snd_depth++;
  t6_snd_capture = 1;
  g_snd_cur_addr = addr;
  g_snd_cur_lr = lr;
  {
    const double f0 = T6NowMs();
    fn(ctx, base);
    snd_time.fn = T6NowMs() - f0;
  }
  g_snd_cur_addr = g_snd_cur_lr = 0;
  t6_snd_capture = 0;
  g_snd_depth--;
  g_snd_caps[0] += g_cap.size();
  g_snd_caps[1]++;
  rec.r3 = ctx.r3.u64;
  rec.f1 = ctx.f1.f64;
  // T6_SND_TRACE=<from>,<to>: every live sound call of those session frames -> <T6_NET_DUMP_DIR>/snd_trace.txt
  if (static const char* tr = T6Env("T6_SND_TRACE"); tr && T6NetRunning()) {
    static uint32_t from = std::strtoul(tr, nullptr, 0), to = std::strtoul(std::strchr(tr, ',') ? std::strchr(tr, ',') + 1 : tr, nullptr, 0);
    static FILE* f = [] { const char* d = T6Env("T6_NET_DUMP_DIR"); return T6Open(fmt::format("{}/snd_trace.txt", d ? d : "."), "w"); }();
    const uint32_t fr = T6NetFrameNo();
    if (f && fr >= from && fr <= to) {
      std::fprintf(f, "%u %08X lr %08X nth %u r3 %08llX f1 %g sp %08X\n", fr, addr, lr, nth,
                   static_cast<unsigned long long>(rec.r3), rec.f1, sp);
      std::fflush(f);
    }
  }
  // netplay diagnostics: hash of the results the game got from the sound library in live frames (T6_RAND_LOG)
  g_snd_result_hash = (g_snd_result_hash ^ (static_cast<uint64_t>(addr) << 32 | static_cast<uint32_t>(rec.r3))) * 0x100000001b3ull;
  g_snd_result_calls++;
  // Writes into memory a load restores (not the stack around the caller's frame)
  std::sort(g_cap.begin(), g_cap.end());
  uint32_t covered = 0;
  for (const auto& [a, n] : g_cap) {
    const uint32_t from = std::max(a, covered), to = a + n;
    // not the stack around the caller's frame, not the library's own heap (live anyway)
    // nor the library's static data in the XEX data/bss (kept live by a load, fine map kinds 4/8): replaying an old
    // value there would undo what the library did since
    const bool lib_static = from - 0x82726000u < 0x692000u && (t6_fine_data[(from - 0x82726000u) >> 4] & 0x0C);
    if (from >= to || (from >= sp - 0x10000 && from < sp + 0x1000) || T6InLibHeap(from) || lib_static) {
      covered = std::max(covered, to);
      continue;
    }
    rec.writes.emplace_back(from, to - from);
    rec.data.insert(rec.data.end(), base + from, base + to);
    covered = to;
  }
  // the bytes before the call: of the first store that touched each byte (stores walked backwards, the first wins)
  if (!rec.writes.empty()) {
    rec.before = rec.data;
    std::vector<size_t> offs(rec.writes.size());
    size_t o = 0;
    for (size_t i = 0; i < rec.writes.size(); i++) {
      offs[i] = o;
      o += rec.writes[i].second;
    }
    for (auto it = g_cap_old.rbegin(); it != g_cap_old.rend(); ++it) {
      auto w = std::upper_bound(rec.writes.begin(), rec.writes.end(), it->a,
                                [](uint32_t a, const std::pair<uint32_t, uint32_t>& x) { return a < x.first; });
      if (w == rec.writes.begin()) continue;
      --w;
      const uint32_t from = w->first, to = w->first + w->second;
      for (uint32_t b = 0; b < it->n; b++) {
        const uint32_t a = it->a + b;
        if (a >= from && a < to) rec.before[offs[w - rec.writes.begin()] + (a - from)] = it->old[b];
      }
    }
  }
  g_snd_recs[frame].push_back(std::move(rec));
  g_snd_count++;
  if (const uint64_t n = g_snd_stats[0] + g_snd_stats[1] + g_snd_stats[2]; n % 5000 == 0) {
    REXLOG_INFO("T6 sound boundary: {} live, {} replayed ({} writes), {} unmatched in re-simulation, {} records; {} device "
                "updates replayed; write groups {} put back, {} refused",
                g_snd_stats[0], g_snd_stats[1], g_snd_stats[3], g_snd_stats[2], g_snd_count, g_snd_dev, g_snd_refused[1], g_snd_refused[0]);
  }
}

// The recorded library frame update (sub_82571FD8, called from sub_82340600 at 82340614) of the frame that is being
// re-simulated: its writes into the game memory again, without running it (see the sub_82340600 hook).
void T6SndReplayDeviceUpdate(uint8_t* base) {
  const auto it = g_snd_recs.find(T6SimFrame());
  if (it == g_snd_recs.end()) return;
  for (const SndRec& r : it->second) {
    if (r.addr != 0x82571FD8u || r.lr != 0x82340614u) continue;
    SndReplayWrites(base, r);
    g_snd_dev++;
    return;
  }
}
