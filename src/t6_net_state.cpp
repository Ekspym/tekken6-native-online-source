// Tekken 6 netplay - input recording / replay, state hashes, snapshot ranges (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

namespace t6np {



std::vector<Range> ParseRanges(const char* rs) {
  std::vector<Range> out;
  for (const char* c = rs; c && *c;) {
    char* e = nullptr;
    const uint32_t lo = std::strtoul(c, &e, 16);
    if (*e != '-') break;
    const uint32_t hi = std::strtoul(e + 1, &e, 16);
    out.push_back({lo, hi});
    c = *e == ',' ? e + 1 : e;
  }
  return out;
}

uint64_t Fnv(const uint8_t* m, uint32_t n) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint32_t i = 0; i + 8 <= n; i += 8) {
    uint64_t w;
    std::memcpy(&w, m + i, 8);
    h = (h ^ w) * 0x100000001b3ull;
  }
  return h;
}

// FNV-1a of a page with every kernel handle value (0xF80000xx..0xF8FFFFFF, big endian words) counted as 0: the runtime
// numbers its objects in creation order and one PC makes a few more (audio device: two PCs at the hold point differed
// only in such words, A7342000 +8A4, A748F000 +F38 - user 2026-10-12). The game never decides on a handle value.
uint64_t FnvNoHandles(const uint8_t* p) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint32_t i = 0; i < 0x1000; i += 4) {
    const bool handle = p[i] == 0xF8 && p[i + 1] == 0x00;  // F800xxxx
    for (uint32_t k = 0; k < 4; k++) h = (h ^ (handle ? 0 : p[i + k])) * 0x100000001b3ull;
  }
  return h;
}

std::atomic<bool> g_t6_holding{false};
// Fast start (user: "tohle trva zbytecne dlouho"): the menu path up to the hold point / the session start runs without
// the 60 per second pace - the guest vblank at 1 ms and the audio frames dropped instead of played (rex_audio_fast,
// sdl_audio_driver.cpp). Everything stays step-locked (audio lockstep, virtual time base, loader waits), only the
// real-time wait is gone. Over once the game is parked or the session starts. T6_NET_FASTBOOT=0 = real time.
std::atomic<bool> g_net_fast_over{false};
// T6_NET_SAMPLE=1: where does a live frame go? A helper thread reads the guest lr (return address of the last call
// made) of the main thread and of the draw worker ~5000 times a second and counts them per phase: 1 = live game
// frame, 2 = re-simulated frame, 0 = frame boundary (not counted). At session frame 3000 -> <dump dir>/samples.txt
// (tools/sample_report.py names the functions). No guessing which part of the frame is expensive.
std::atomic<int> g_main_phase{0};
std::atomic<bool> g_watch_pause{false};  // no session running: the hang watch has nothing to watch  // main thread parked at the hold point (T6_NET_HOLD), waiting for 'start'
// Where the main thread is inside the session's frame boundary (hang watch: a wait there has no guest position)
std::atomic<const char*> g_net_where{""};


State& state() {
  static State s;
  return s;
}

// ---- save/restore test ----



// Guest ranges saved by default: the game's own data and heaps (T6_SNAP_RANGES=lo-hi,... overrides).
//  - XEX image after the code: .data/.bss
//  - game heaps created by sub_8223B6A8 at boot (physical, contiguous): 0xA7290000-0xABA90000
// Not saved: GPU pools 0xABCA0000 (240 MB) / 0xBACA0000 (80 MB) / 0xABA90000 (2 MB) allocated via 0x8252xxxx,
// Direct3D command buffers, thread stacks 0x70000000+ and the virtual load buffers 0x40000000+.
std::vector<Range>& SnapRanges() {
  static std::vector<Range> r = [] {
    if (const char* p = T6Env("T6_SNAP_RANGES"); p && *p) return ParseRanges(p);
    return std::vector<Range>{{0x82726AC8u, 0x82DB0000u}, {0xA7290000u, 0xABA90000u}};
  }();
  return r;
}


}  // namespace t6np
