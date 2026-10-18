// Tekken 6 netplay - job workers and the draw thread idle around a save / load, global locks (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

namespace t6np {
// ---- Job workers idle (PS3: wait for the SPURS jobs before a save / load) ----
// The game's any_workerThread pool waits on one job semaphore (T6_JOB_SEM, default F8000080) with T6_JOB_WORKERS
// threads (default 5). A save or load while a job runs tears the objects it updates (a worker crashed at the restore).
// (namespace t6np continues)
// The kernel handles differ between runs / instances (creation order): read them from the game objects (PS3: object
// addresses, never handle numbers). Job semaphore: job pool control 0x829CA158. Net worker (loop sub_82519EC0, object
// W = 0x829F4300, found by the default handles in st64): condition lock *(W+144), wake event *(W+148) (sub_82528C68 / sub_82528C10).
// T6_JOB_SEM / T6_NET_LOCK / T6_NET_WAIT override.
uint32_t GameHandle(const char* env, uint32_t addr, uint32_t fallback) {
  if (const char* v = T6Env(env)) return std::strtoul(v, nullptr, 16);
  const uint32_t h = T6Rd32(addr);
  return (h >> 24) == 0xF8 ? h : fallback;
}
// the net worker's wake event (its condition wait), T6_NET_WAIT overrides
uint32_t NetWakeHandle() { return GameHandle("T6_NET_WAIT", 0x829F4394u, 0xF8000078u); }
void LogGameHandles() {
  static bool logged = false;
  if (logged) return;
  logged = true;
  REXLOG_INFO("T6 snaptest: job semaphore {:08X}, net worker lock {:08X} wake {:08X}",
              GameHandle("T6_JOB_SEM", 0x829CA158u, 0), GameHandle("T6_NET_LOCK", 0x829F4390u, 0),
              GameHandle("T6_NET_WAIT", 0x829F4394u, 0));
}
// Work queues with a "wait until everything submitted is done" (sub_823BA530: lock *(Q+44); while (*(Q+32) != 0 &&
// *(Q+32) != *(Q+36)) wait on *(Q+40)); Q+32 = submitted, Q+36 = done, counted by the queue's own thread). The
// counters are game memory, the thread is not rolled back: a snapshot taken while a job was on its way restores
// "submitted != done" after the thread has long finished - nobody signals any more and the main thread waits for
// ever. 2026-10-13, random input test, run 14: "load of frame 858 at frame 860", then the main thread stayed in
// NtSignalAndWaitForSingleObjectEx (lr 824BD244, chain 823BA584 822DFA58 822E29E0 ...) until the test ended.
// So no save / load while such a queue has work on its way. The queues are static objects: the two of
// sub_822DFA10 and every one a flush is called on (hook below).
std::mutex g_drain_mutex;
std::vector<uint32_t> g_drain_queues = {0x82935CC0u, 0x82935CF0u};
uint64_t g_drain_waits = 0;
bool QueuesDrained() {
  std::lock_guard<std::mutex> lock(g_drain_mutex);
  for (const uint32_t q : g_drain_queues) {
    const uint32_t submitted = T6Rd32(q + 32);
    const uint32_t done = T6Rd32(q + 36);
    if (submitted != 0 && submitted != done) return false;
  }
  return true;
}
bool WaitWorkersIdle(int max_ms) {
  if (!QueuesDrained()) {
    const double d0 = T6NowMs();
    int t = 0;
    for (; t < max_ms * 10 && !QueuesDrained(); t++) {
      Sleep(0);
      if ((t & 15) == 15) Sleep(1);
    }
    if (++g_drain_waits <= 20 || !QueuesDrained()) {
      REXLOG_INFO("T6 snaptest: a work queue had jobs on their way at a save / load: waited {:.2f} ms{} ({} times so far)",
                  T6NowMs() - d0, QueuesDrained() ? "" : " - STILL busy", g_drain_waits);
    }
  }
  using WaitersFn = int (*)(uint32_t);
  const auto fn = T6Rex().waiters;
  LogGameHandles();
  const uint32_t sem = GameHandle("T6_JOB_SEM", 0x829CA158u, 0xF8000080u);
  static const int workers = T6EnvInt("T6_JOB_WORKERS", 5);
  if (!fn) return false;
  // PS3 SPURS rule: no save / load while a job runs. The job queue of the pool 0x829CA080 at 0x829EB9B4 (sub_82525638:
  // free list +24/+40/+36, used list +16/+20, count +12) is restored together with its nodes (keeping it live broke the
  // lists: crash 8251C1B0, st62 / n3); its used list always holds 1 node even with every worker idle (st64).
  const auto queued = [] { return T6Rd32(0x829EB9B4u + 12); };
  // all workers blocked AND no released-but-untaken count on the semaphore (else a woken worker takes a job from the
  // restored queue: crash 8251BD3C r31 = 0 after a rollback, v12)
  const auto cnt = T6Rex().semaphore_count;
  for (int t = 0; t < max_ms * 10; t++) {
    if (fn(sem) >= workers && (!cnt || cnt(sem) <= 0)) return true;
    Sleep(0);
    if ((t & 15) == 15) Sleep(1);
  }
  REXLOG_WARN("T6 snaptest: job workers waiting {} of {}, queue {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
              fn(sem), workers, queued(),
              T6Rd32(0x829EB9B4u),
              T6Rd32(0x829EB9B8u),
              T6Rd32(0x829EB9BCu),
              T6Rd32(0x829EB9C4u),
              T6Rd32(0x829EB9C8u),
              T6Rd32(0x829EB9CCu),
              T6Rd32(0x829EB9D0u),
              T6Rd32(0x829EB9D4u),
              T6Rd32(0x829EB9D8u),
              T6Rd32(0x829EB9DCu),
              T6Rd32(0x829EB9E0u));
  return false;
}
// net_workerThread works on its queue all the time (condition variable: lock F8000074, wake F8000078); a save or load
// while it runs tears the queue (crash in 826B6DE0 at the restore). The lock is held across the save / load
// (T6_NET_LOCK=<handle>, 0 = off).
// Saves only (PS3 RB_DRAW_IDLE_RSX=2): the net worker also counts as idle while it is blocked on a GPU fence
// (sub_824D3F08 calling sub_824D9458, called from sub_8253BDF8 / sub_82522AB0): its CPU work of the frame is done
// and a save does not change memory. Loads keep the full wait. T6_SAVE_NET_FENCE=0 = always the full wait.
// Its context comes from its last wait on the wake handle (rex_last_waiter_context).
uint64_t g_net_fence_idle = 0, g_net_saves = 0;
bool NetWorkerInFenceWait(uint32_t wait_h) {
  using CtxFn = void* (*)(uint32_t);
  const auto cf = T6Rex().last_waiter_context;
  const auto* c = cf ? static_cast<const PPCContext*>(cf(wait_h)) : nullptr;
  if (!c) return false;
  const uint32_t lr = static_cast<uint32_t>(c->lr);
  return (lr >= 0x824D3F08u && lr < 0x824D3FD0u) || (lr >= 0x824D9458u && lr < 0x824D9530u);
}
NetLockHold::NetLockHold(bool for_save) {
  const auto fn = T6Rex().hold_object;
  const uint32_t lock = GameHandle("T6_NET_LOCK", 0x829F4390u, 0xF8000074u);
  if (!fn || !lock) return;
  h = lock;
  // PS3 wait_for_draw_idle: the worker must be idle, i.e. blocked in its condition wait (T6_NET_WAIT, default
  // F8000078, which releases the lock), not just between two items. Take the lock, check, else let it run.
  const auto wf = T6Rex().waiters;
  const uint32_t wait_h = NetWakeHandle();
  static const bool fence_ok = T6EnvDefaultOn("T6_SAVE_NET_FENCE");
  if (for_save) g_net_saves++;
  for (int t = 0; t < 2000; t++) {
    held = fn(h, 1, 50) == 1;
    if (!held) break;
    if (!wf || wf(wait_h) >= 1) return;
    fn(h, 0, 0);
    held = false;
    // T6_NET_WORKER_DIAG=1 (PS3 RB_DRAW_WAIT_DIAG): where the draw worker is while a save waits for it - histogram of
    // its guest lr / last indirect target at every poll, the top entries are logged every 600 saves
    if (static const bool diag = T6Env("T6_NET_WORKER_DIAG") != nullptr; diag && for_save) {
      const auto cf = T6Rex().last_waiter_context;
      static std::map<std::pair<uint32_t, uint32_t>, uint64_t> hist;
      static uint64_t polls = 0, last_saves = 0;
      if (const auto* c = cf ? static_cast<const PPCContext*>(cf(wait_h)) : nullptr) {
        hist[{static_cast<uint32_t>(c->lr), c->last_indirect_target}]++;
        polls++;
      }
      if (g_net_saves >= last_saves + 600) {
        last_saves = g_net_saves;
        std::vector<std::pair<uint64_t, std::pair<uint32_t, uint32_t>>> v;
        v.reserve(hist.size());
        for (const auto& [k, n] : hist) v.emplace_back(n, k);
        std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        std::string t;
        for (size_t i = 0; i < v.size() && i < 8; i++) t += fmt::format(" lr {:08X} ind {:08X} x{}", v[i].second.first, v[i].second.second, v[i].first);
        REXLOG_INFO("T6 snap: draw worker while saves waited ({} polls):{}", polls, t);
        hist.clear();
        polls = 0;
      }
    }
    if (for_save && fence_ok && NetWorkerInFenceWait(wait_h)) {
      if (g_net_fence_idle++ % 600 == 0) {
        REXLOG_INFO("T6 snap: net worker on a GPU fence counted idle at the save ({} of {} saves)", g_net_fence_idle, g_net_saves);
      }
      return;
    }
    Sleep(t < 100 ? 0 : 1);
  }
  REXLOG_WARN("T6 snaptest: net worker not idle (lock {:08X} {})", h, held ? "held" : "not taken");
}

NetLockHold::~NetLockHold() {
  const auto fn = T6Rex().hold_object;
  if (held) fn(h, 0, 0);
}

// (end of an inner block of namespace t6np)
}  // namespace t6np
