// Tekken 6 netplay - sampler of the main thread (T6_NET_SAMPLE) (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Sampler (T6_NET_SAMPLE=1) ----
namespace t6np {
std::mutex g_smp_m;
std::unordered_map<uint32_t, uint64_t> g_smp[5];  // live main, re-simulated main, draw worker (while the main thread is in a live frame or at the boundary)
uint64_t g_smp_n[5] = {};  // [3] = audio worker inside a guest callback, [4] = XAudio2 processing thread while busy
uint64_t g_smp_worker_busy_boundary = 0, g_smp_boundary = 0;
// (research 2026-10-21) re-simulated samples outside every task step: the systemroot update (called by the task walker
// at 822188F8) is on the chain once, a task's own step twice or more; key = innermost lr + 5 callers
std::unordered_map<std::string, uint64_t> g_smp_root_self;
uint64_t g_smp_root_self_n = 0;
}  // namespace t6np
void T6SampleStart() {
  static bool started = false;
  if (started) return;
  started = true;
  std::thread([] {
    const auto cf = T6Rex().last_waiter_context;
    const auto wf = T6Rex().waiters;
    const uint32_t wait_h = NetWakeHandle();
    const auto acf = T6Rex().audio_callback_context;
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    const int64_t step = f.QuadPart / 5000;
    int64_t next = t.QuadPart;
    for (;;) {
      do { QueryPerformanceCounter(&t); YieldProcessor(); } while (t.QuadPart < next);
      next += step;
      if (t.QuadPart > next + 50 * step) next = t.QuadPart;
      if (!g_net_running) { Sleep(5); continue; }
      const int ph = g_main_phase.load(std::memory_order_relaxed);
      std::lock_guard lock(g_smp_m);
      if (ph == 1 || ph == 2) {
        const PPCContext* c = ph == 2 && t6_frame_ctx ? t6_frame_ctx : g_main_ctx;
        if (c) { g_smp[ph - 1][static_cast<uint32_t>(c->lr)]++; g_smp_n[ph - 1]++; }
        if (c && ph == 2) {
          uint32_t chain[32];
          int depth = 0, walker = 0;
          for (uint32_t sp = c->r1.u32; depth < 32 && sp >= 0x70000000u && sp < 0x7FFF0000u;) {
            const uint32_t next = T6Rd32(sp);
            // (the outermost frame's back chain = the stack top, 64K aligned, with no mapped page above it)
            if (next <= sp || next - sp > 0x10000u || (next & 0xFFFFu) == 0) break;
            chain[depth] = T6Rd32(next - 8);
            walker += chain[depth] == 0x822188F8u;
            depth++;
            sp = next;
          }
          if (walker == 1) {
            std::string key = fmt::format("{:08X} <", static_cast<uint32_t>(c->lr));
            for (int i = 0; i < depth && i < 5; i++) key += fmt::format(" {:08X}", chain[i]);
            g_smp_root_self[key]++;
            g_smp_root_self_n++;
          }
        }
      }
      // the draw worker: busy = not waiting on its wake condition
      if (ph != 2 && cf && wf && wf(wait_h) < 1) {
        if (const auto* wc = static_cast<const PPCContext*>(cf(wait_h))) { g_smp[2][static_cast<uint32_t>(wc->lr)]++; g_smp_n[2]++; }
        g_smp_worker_busy_boundary += ph == 0;
      }
      g_smp_boundary += ph == 0;
      if (acf) {
        if (const auto* ac = static_cast<const PPCContext*>(acf())) { g_smp[3][static_cast<uint32_t>(ac->lr)]++; g_smp_n[3]++; }
      }
      if (cf && wf && wf(0x82984BF0u) < 1) {  // (its event: AudioStep)
        if (const auto* xc = static_cast<const PPCContext*>(cf(0x82984BF0u))) { g_smp[4][static_cast<uint32_t>(xc->lr)]++; g_smp_n[4]++; }
      }
    }
  }).detach();
}
void T6SampleDump(uint32_t frame) {
  if (frame != 3000) return;
  std::lock_guard lock(g_smp_m);
  const char* d = T6Env("T6_NET_DUMP_DIR");
  FILE* f = T6Open(fmt::format("{}/samples.txt", d ? d : "."), "w");
  if (!f) return;
  std::fprintf(f, "# 5000 samples per second; boundary samples %llu, of them the draw worker busy %llu\n",
               static_cast<unsigned long long>(g_smp_boundary), static_cast<unsigned long long>(g_smp_worker_busy_boundary));
  static const char* const names[5] = {"live", "resim", "worker", "audio", "xaudio"};
  for (int k = 0; k < 5; k++) {
    std::vector<std::pair<uint64_t, uint32_t>> v;
    for (const auto& [lr, n] : g_smp[k]) v.emplace_back(n, lr);
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::fprintf(f, "%s total %llu\n", names[k], static_cast<unsigned long long>(g_smp_n[k]));
    for (const auto& [n, lr] : v) std::fprintf(f, "%s %08X %llu\n", names[k], lr, static_cast<unsigned long long>(n));
  }
  {
    std::vector<std::pair<uint64_t, std::string>> v;
    for (const auto& [k, n] : g_smp_root_self) v.emplace_back(n, k);
    std::sort(v.rbegin(), v.rend());
    std::fprintf(f, "root-self total %llu\n", static_cast<unsigned long long>(g_smp_root_self_n));
    for (size_t i = 0; i < v.size() && i < 200; i++) std::fprintf(f, "root-self %llu %s\n", static_cast<unsigned long long>(v[i].first), v[i].second.c_str());
  }
  std::fclose(f);
  REXLOG_INFO("T6 net: samples written ({} live, {} re-simulated, {} worker)", g_smp_n[0], g_smp_n[1], g_smp_n[2]);
}
