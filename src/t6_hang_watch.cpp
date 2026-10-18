// Tekken 6 netplay - swap queue log and hang watch (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"


namespace t6np {
// Swap queue of the D3D vblank callback sub_824D9B38 (device 0x40021D80): read +16700 / write +16704, 8 entries at
// +16572 (0 = release the GPU wait / register value, target vblank +16576), vblank count +16548. The swap
// (sub_824D9EB0) makes the GPU wait until *(device[+10900] + 4) is 0, which the callback writes when the entry is due.
void LogSwapQueue(const char* tag) {
  const uint32_t dev = 0x40021D80u, wb2 = T6Rd32(dev + 10900);
  const uint32_t flag = wb2 + 4 + (wb2 >= 0xE0000000u ? 0x1000u : 0u);
  std::string q;
  for (uint32_t k = 0; k < 8; k++) q += fmt::format(" {:08X}/{:08X}", T6Rd32(dev + 16572 + k * 8), T6Rd32(dev + 16576 + k * 8));
  REXLOG_WARN("T6 {}: swap queue r {} w {} vblank {:08X} +16564 {:08X} +21556 {:08X} flag @{:08X} = {:08X}; entries:{}", tag,
              T6Rd32(dev + 16700), T6Rd32(dev + 16704), T6Rd32(dev + 16548), T6Rd32(dev + 16564), T6Rd32(dev + 21556), wb2 + 4,
              wb2 ? T6Rd32(flag) : 0xEEEEEEEEu, q);
}
}  // namespace t6np

// Hang watch after a restore: when the pad step stops advancing, log where the main thread is (guest LR + back chain
// of its PPC context, read from another thread) a few times.
namespace t6np {
void StartHangWatch(const uint32_t* step) {
  static bool started = false;
  if (started || !g_main_ctx) return;
  started = true;
  std::thread([step] {
    uint32_t last = *step, same = 0, logged = 0;
    // T6_HANG_MS=<n>: poll every n/4 ms and report after n ms (default 2000); a short value finds the 0.3 s stalls
    static const int hang_ms = T6Env("T6_HANG_MS") ? std::max(40, T6ToInt(T6Env("T6_HANG_MS"))) : 2000;
    while (logged < (hang_ms < 2000 ? 60u : 8u)) {
      Sleep(hang_ms / 4);
      const uint32_t now = *step;
      if (now != last || g_t6_holding || g_watch_pause) { last = now; same = 0; continue; }
      if (++same < 4) continue;
      same = 0;
      // (a re-simulated frame runs on a copy of the main context)
      const PPCContext* c = t6_frame_ctx ? t6_frame_ctx : g_main_ctx;
      REXLOG_WARN("T6 hang watch: main thread in '{}'{}", g_net_where.load(), t6_frame_ctx ? " (re-simulated frame)" : "");
      {
        const auto irq = T6Rex().interrupt_info;
        uint32_t ilr = 0;
        const long long ms = irq ? irq(&ilr) : -2;
        if (ms >= 0) REXLOG_WARN("T6 hang watch: an interrupt callback has been running for {} ms (holds the global lock), lr {:08X}", ms, ilr);
        else REXLOG_WARN("T6 hang watch: no interrupt callback running ({})", ms);
      }
      const auto readable = [](uint32_t a) {
        MEMORY_BASIC_INFORMATION mbi{};
        return VirtualQuery(g_t6_membase + a, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
               !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY));
      };
      const auto chain_of = [&](uint32_t sp) {
        std::string chain;
        for (int i = 0; i < 16 && sp >= 0x70000000u && sp < 0x7FFF0000u; i++) {
          if (!readable(sp)) break;
          const uint32_t next = T6Rd32(sp);
          if (next <= sp || next - sp > 0x10000u) break;
          if (!readable(next - 8)) break;
          char buf[12];
          std::snprintf(buf, sizeof(buf), " %08X", T6Rd32(next - 8));
          chain += buf;
          sp = next;
        }
        return chain;
      };
      const std::string chain = chain_of(c->r1.u32);
      std::string obj;
      const uint32_t r3v = c->r3.u32;
      for (uint32_t k = 0; k < 32 && r3v >= 0x10000u && readable(r3v); k += 4) {
        char buf[12];
        std::snprintf(buf, sizeof(buf), " %08X", T6Rd32(r3v + k));
        obj += buf;
      }
      REXLOG_WARN("T6 hang watch: [r3]:{}", obj);
      // r3 = critical section (RtlEnterCriticalSection): owner KTHREAD at +24 -> that thread's name and position
      // Follow the chain: CS -> owner thread -> the CS that thread waits for (its r3) -> ... (lock order cycles)
      uint32_t cs = r3v;
      for (int level = 0; level < 4 && cs >= 0x10000u && readable(cs + 24); level++) {
        const auto lf = T6Rex().live_guest_objects;
        std::vector<uint32_t> lv(lf ? lf(nullptr, 0) * 2 + 64 : 0);
        const uint32_t ln = lf ? std::min<uint32_t>(lf(lv.data(), static_cast<uint32_t>(lv.size() / 2)), static_cast<uint32_t>(lv.size() / 2)) : 0;
        bool reg = false;
        for (uint32_t k = 0; k < ln; k++) reg |= (cs >= lv[k * 2] && cs < lv[k * 2 + 0] + lv[k * 2 + 1]);
        const uint32_t owner = T6Rd32(cs + 24);
        REXLOG_WARN("T6 hang watch: level {} CS {:08X} live registry {} lock {:08X} recursion {:08X} owner {:08X}", level, cs, reg,
                    T6Rd32(cs + 16), T6Rd32(cs + 20), owner);
        const auto tf = T6Rex().thread_by_kthread;
        char name[64] = {};
        const auto* oc = (tf && owner) ? static_cast<const PPCContext*>(tf(owner, name, sizeof(name))) : nullptr;
        if (!oc) break;
        REXLOG_WARN("T6 hang watch: owner {:08X} '{}' lr {:08X} r3 {:08X} r31 {:08X} chain:{}", owner, name,
                    static_cast<uint32_t>(oc->lr), oc->r3.u32, oc->r31.u32, chain_of(oc->r1.u32));
        if (oc == c) break;
        cs = oc->r3.u32;
      }
      {
        // D3D device 0x40021D80: +10908 last issued fence, +10896 -> fence the GPU wrote back (sub_824D3F08 waits)
        const uint32_t dev = 0x40021D80u, wb = T6Rd32(dev + 10896);
        const uint64_t gp = g_t6_gpu_ptrs ? g_t6_gpu_ptrs() : 0;
        REXLOG_WARN("T6 hang watch: gpu ring read {:X} write {:X}", static_cast<uint32_t>(gp >> 32), static_cast<uint32_t>(gp));
        REXLOG_WARN("T6 hang watch: d3d issued {:08X} writeback @{:08X} = {:08X}, r30 {:08X}", T6Rd32(dev + 10908), wb,
                    readable(wb + (wb >= 0xE0000000u ? 0x1000u : 0u)) ? T6Rd32(wb + (wb >= 0xE0000000u ? 0x1000u : 0u)) : 0xEEEEEEEEu, c->r30.u32);
        LogSwapQueue("hang watch");
      }
      REXLOG_WARN("T6 hang watch: step {} stuck; main lr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r31 {:08X} last_indirect {:08X} chain:{}",
                  now, static_cast<uint32_t>(c->lr), c->r1.u32, c->r3.u32, c->r4.u32, c->r31.u32, c->last_indirect_target, chain);
      logged++;
      if (hang_ms >= 2000) Sleep(1500);
      else {
        // the draw worker too (where it is while the main thread waits)
        const auto cf = T6Rex().last_waiter_context;
        const uint32_t wait_h = NetWakeHandle();
        if (const auto* wc = cf ? static_cast<const PPCContext*>(cf(wait_h)) : nullptr) {
          REXLOG_WARN("T6 hang watch: draw worker lr {:08X} r3 {:08X} last_indirect {:08X} chain:{}", static_cast<uint32_t>(wc->lr), wc->r3.u32,
                      wc->last_indirect_target, chain_of(wc->r1.u32));
        }
        // wait until it moves again
        while (*step == now) Sleep(5);
        last = *step;
      }
    }
  }).detach();
}
}  // namespace t6np
