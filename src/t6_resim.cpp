// Tekken 6 netplay - re-simulated frames: task profile, skip list, draw root (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Task step profile of re-simulated frames (PS3 RB_TASK_PROFILE) ----
// sub_822188C8(node, r4) = one task step of the walker sub_82218C80 (PS3 FUN_000ff344): r = (*(node+48))(node), then
// the next node. T6_TASK_PROFILE=1: time per step function / first word of the node in re-simulated frames (and the
// same for live frames), logged every 100 re-simulated frames -> which tasks a re-simulated frame pays for.
extern "C" void __imp__sub_822188C8(PPCContext& ctx, uint8_t* base);
std::map<uint32_t, int> g_crowd_pages, g_other_pages;  // (research, T6_CROWD_PAGES)
namespace t6np {
struct TaskCost { double ms = 0; uint64_t n = 0; };
std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, TaskCost> g_task_cost[2];  // [resim]: (step fn, *(node), *(node-84), *(node-88))
int g_task_depth = 0;
double g_task_child = 0;
}  // namespace t6np
// Tasks left out of re-simulated frames (PS3 RB_RESIM_SKIP + draw root): only in their steady state (generic step
// sub_82218280 = call the object's update, vtable +20), by the vtable at the node. tcb_drawroot_t 82007940 (draw tree,
// 4.4 ms per frame). NOT tcb_device_sound_t 82016944 (6 ms in a re-simulated frame): skipping it crashed the main
// thread in XAudio2 code (824EED38, read of 0x3F800000) at the first rollback - the sound boundary replays the sound
// calls of a re-simulated frame and needs the task to run. The step result is the walker's "r == 0" path without the
// call. T6_RESIM_SKIP=0 switches it off.
// T6_SPIKE_PROFILE=1 (2026-10-17: a third of the long frames of the user's matches are live frames with 14-17 ms of
// work instead of ~6): the task self times of every live frame; a frame whose tasks took more than 10 ms goes to the
// log with its 6 most expensive tasks (vtable) and the draw times ("T6 spike").
std::unordered_map<uint32_t, double> g_spike;
double g_spike_total = 0;
void T6SpikeNote(uint32_t vt, double ms) {
  static const bool on = T6Env("T6_SPIKE_PROFILE") != nullptr;
  if (!on) return;
  g_spike[vt] += ms;
  g_spike_total += ms;
}
void T6SpikeFrameEnd() {
  static const bool on = T6Env("T6_SPIKE_PROFILE") != nullptr;
  if (!on) return;
  if (g_spike_total > 10.0 && !g_net_resim) {
    std::vector<std::pair<double, uint32_t>> v;
    v.reserve(g_spike.size());
    for (const auto& [k, ms] : g_spike) v.emplace_back(ms, k);
    std::sort(v.rbegin(), v.rend());
    std::string line;
    for (size_t i = 0; i < v.size() && i < 6; i++) line += fmt::format(" {:08X}={:.1f}", v[i].second, v[i].first);
    REXLOG_INFO("T6 spike: frame {} mode {} tasks {:.1f} ms:{}", g_net_frame, T6GameMode(), g_spike_total, line);
  }
  g_spike.clear();
  g_spike_total = 0;
}
extern "C" void __imp__sub_82219788(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822188C8(PPCContext& ctx, uint8_t* base) {
  // PS3 RB_RESIM_SKIP (play_online.bat: 90cbb0 ZSStgSnowTcb, 90d1d0 tcb_stage_particle_t, 915690 tcb_owwindow_t, 910798
  // tcb_device_sound_t, 91de98 CINetMatching, 90f0a8 tcb_glow_swing_t, 90c598 ZSUVScrManageTcb, 905ae8
  // tcb_kkShaderManager, 9063e0-906a80 tcb_kkMob*): visual tasks are not stepped in a re-simulated frame; only in their
  // steady state (generic step sub_82218280), the walker's "r == 0" path is taken instead. X360 vtables from the RTTI
  // (an earlier analysis). The draw root and the sound device are handled by their steady state functions (sub_8221CA68,
  // sub_82340600) - skipping those two whole tasks broke the film / the sound objects. T6_RESIM_SKIP=0 switches it off.
  // Not in the list: glow swing 8201552C, shader manager 8200303C, UV scroll 8200DFB4 (PS3: ~45 us together, and skipping
  // them loses small effects like hit sparks) and MMC_TCB_3DTRACK_BANK_T 82016804 (sk1: crash at frame ~4640 in its
  // update, lr 8234E760, stale sound object after a rollback).
  static const bool skip_on = T6EnvDefaultOn("T6_RESIM_SKIP");
  if (skip_on && g_net_resim && t6_thread_bit == 0) {
    const uint32_t node = ctx.r3.u32;
    if (T6Rd32(node + 48) == 0x82218280u) {
      static const uint32_t skip_vt[] = {
          0x82003408u, 0x82003494u, 0x8200353Cu, 0x8200366Cu, 0x820036ECu, 0x8200378Cu, 0x820037ECu, 0x8200386Cu,  // tcb_kkMob*
          0x82003918u, 0x82003984u, 0x82003A5Cu, 0x82003ADCu, 0x82003B7Cu, 0x82003C10u,                            // tcb_kkMob*
          0x82010E08u,  // tcb_stage_particle_t
          0x8200E364u,  // ZSStgSnowTcb
          0x8207C1C8u,  // tcb_owwindow_t
          0x8208350Cu,  // CINetMatching
      };
      const uint32_t vt = T6Rd32(node);
      bool skip = false;
      for (const uint32_t v : skip_vt) skip |= v == vt;
      if (skip) {
        const uint32_t flags = T6Rd32(node + 60) | 8u;
        *reinterpret_cast<uint32_t*>(base + node + 60) = _byteswap_ulong(flags);
        *reinterpret_cast<uint32_t*>(base + node + 72) = _byteswap_ulong(T6Rd32(node + 72) + 1);
        t6_dirty_mark(node + 60);
        t6_dirty_mark(node + 72);
        if ((ctx.r4.u32 & 0xFF) && (flags & 0x80u)) {
          __imp__sub_82219788(ctx, base);  // r3 = node: past the subtree
        } else {
          ctx.r3.u64 = T6Rd32(node + 16);
        }
        return;
      }
    }
  }
  // T6_CROWD_PAGES=<step> (research): from that pad step on, for 3 steps, the pages each crowd task step writes (the
  // dirty map cleared around the step, then put back) -> "T6 crowd pages" in the log
  if (static const uint32_t at = T6EnvU32("T6_CROWD_PAGES", 0, 0); at && t6_thread_bit == 0 && state().step >= at && state().step < at + 3) {
    static const std::vector<uint32_t> mob(std::begin(kT6CrowdVt), std::end(kT6CrowdVt));
    const uint32_t vt = T6Rd32(ctx.r3.u32);
    if (std::find(mob.begin(), mob.end(), vt) != mob.end()) {
      static std::vector<unsigned char> keep(sizeof(t6_dirty));
      auto& pages = g_crowd_pages;
      static uint32_t steps = 0;
      std::memcpy(keep.data(), t6_dirty, sizeof(t6_dirty));
      std::memset(t6_dirty, 0, sizeof(t6_dirty));
      __imp__sub_822188C8(ctx, base);
      for (uint32_t q = 0; q < sizeof(t6_dirty); q++) {
        if (t6_dirty[q]) pages[q] = 1;
        t6_dirty[q] = keep[q];  // (only what the rest of the frame wrote stays: T6CrowdPagesFrame)
      }
      if (++steps % 211 == 0 || steps < 3) {
        std::string runs;
        for (auto it = pages.begin(); it != pages.end();) {
          uint32_t a0 = it->first, e = a0 + 1;
          for (++it; it != pages.end() && it->first == e; ++it) e++;
          runs += fmt::format(" {:08X}-{:08X}", a0 << 12, e << 12);
        }
        REXLOG_INFO("T6 crowd pages after {} steps: {} pages:{}", steps, pages.size(), runs);
      }
      return;
    }
  }
  static const bool prof = T6Env("T6_TASK_PROFILE") != nullptr || T6Env("T6_SPIKE_PROFILE") != nullptr;
  if (!prof || !T6NetRunning() || t6_thread_bit != 0) {
    __imp__sub_822188C8(ctx, base);
    return;
  }
  const uint32_t node = ctx.r3.u32;
  const auto key = std::make_tuple(T6Rd32(node + 48), T6Rd32(node), T6Rd32(node - 84), T6Rd32(node - 88));
  const int rs = g_net_resim ? 1 : 0;
  const double saved_child = g_task_child;
  g_task_child = 0;
  g_task_depth++;
  const double t0 = T6NowMs();
  __imp__sub_822188C8(ctx, base);
  const double dt = T6NowMs() - t0;
  g_task_depth--;
  auto& c = g_task_cost[rs][key];
  c.ms += dt - g_task_child;  // self time: nested walks (sub tasks) are counted on their own
  if (!rs) T6SpikeNote(std::get<1>(key), dt - g_task_child);
  c.n++;
  g_task_child = saved_child + dt;
  static uint64_t last = 0;
  if (g_task_depth == 0 && g_net_resim_frames >= last + 100) {
    last = g_net_resim_frames;
    for (int k = 0; k < 2; k++) {
      std::vector<std::pair<double, std::string>> rows;
      double total = 0;
      for (const auto& [kk, v] : g_task_cost[k]) {
        total += v.ms;
        rows.emplace_back(v.ms, fmt::format("step {:08X} w0 {:08X} -84 {:08X} -88 {:08X}: {:.1f} ms / {} = {:.3f}", std::get<0>(kk), std::get<1>(kk),
                                            std::get<2>(kk), std::get<3>(kk), v.ms, v.n, v.ms / std::max<uint64_t>(v.n, 1)));
      }
      std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
      REXLOG_INFO("T6 task profile ({}): total {:.1f} ms, {} step kinds", k ? "re-simulated" : "live", total, rows.size());
      for (size_t i = 0; i < rows.size() && i < 14; i++) REXLOG_INFO("T6 task profile ({}):   {}", k ? "re-simulated" : "live", rows[i].second);
      g_task_cost[k].clear();
    }
  }
}


// ---- Draw root in re-simulated frames (PS3 FUN_001060d8 rules) ----
// Steady state of tcb_drawroot_t, sub_8221CA68: this+228 = 1; sub_82218FA0() = walk of the draw subtree (scene, models,
// effects, HUD); then, scene = weak ref 0x8273593C: *(scene+288) == 0 -> draw pass sub_82219098(*(0x82828C60), 0, 1),
// else only the match recording sub_8221B7C0(weak ref 0x82735A74) ("film", read by the KO replay).
// In a re-simulated frame of game mode 4 (fight) only the film recording runs: no draw subtree, no draw pass. From the
// KO on (mode != 4: KO camera, replay, between rounds, intros) everything runs in full (PS3: desync at the next round
// start / crash at the KO otherwise). The film buffer is game memory, so a load rewinds it with the rest.
// ON by default since the released-object tracking below (sv4: 7263 frames, 514 rollbacks, fight state equal, no
// crash). History: the fight state stayed equal (dr1, 1978 frames, 89 rollbacks) and a
// re-simulated frame lost the 4.4 ms, but the main thread crashed like in the two earlier skip experiments: XAudio2
// SetVolume sub_824EECF0 (lr 824EED38; callers sub_82341C88 / sub_823418C0 MMC sound, sub_82576668 / sub_8257C680
// CnuSound) on a voice object 0x302D801C whose vtable word is 0x3F800000 = a stale voice. Three long runs without the
// skip did not crash (ft3, ft4, pd6). Open: why the missing draw exposes it (voice objects 0x30xxxxxx are outside the
// snapshot, the sound library tables that point to them are inside).
// Where the draw root's 4.9 ms of a live frame go (tp1): main thread time in the draw subtree walk sub_82218FA0, in
// the draw pass sub_82219098 and, inside either, in the D3D fence wait sub_824D3F08 (the main thread waiting for the
// GPU). Window averages in the "average frame" log line.
double g_draw_ms[3] = {};
// T6_RESIM_WAITS=1 (2026-10-17, profile: 15 % of a re-simulated frame in sub_824D9458, the poll of the D3D waits):
// main thread time in the D3D waits during re-simulated frames, by wait function and caller (lr), every 300
// re-simulated frames in the log ("T6 resim waits").
void ResimWaitNote(uint32_t fn, uint32_t lr, double ms) {
  static const bool on = T6Env("T6_RESIM_WAITS") != nullptr;
  if (!on || !g_net_resim || t6_thread_bit != 0) return;
  static std::map<std::pair<uint32_t, uint32_t>, std::pair<double, uint64_t>> acc;
  auto& a = acc[{fn, lr}];
  a.first += ms;
  a.second++;
  static uint64_t last = 0;
  if (g_net_resim_frames >= last + 300) {
    std::vector<std::pair<double, std::string>> rows;
    rows.reserve(acc.size());
    for (const auto& [k, v] : acc) rows.emplace_back(v.first, fmt::format("sub_{:08X} from {:08X}: {:.1f} ms / {}", k.first, k.second, v.first, v.second));
    std::sort(rows.rbegin(), rows.rend());
    std::string line;
    for (size_t i = 0; i < rows.size() && i < 10; i++) line += " | " + rows[i].second;
    REXLOG_INFO("T6 resim waits over {} re-simulated frames:{}", g_net_resim_frames - last, line);
    last = g_net_resim_frames;
    acc.clear();
  }
}
#define T6_RESIM_WAIT(fn, addr)                                              extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base);               extern "C" void fn(PPCContext& ctx, uint8_t* base) {                         const uint32_t lr = static_cast<uint32_t>(ctx.lr);                         const double t0 = T6NowMs();                                                 __imp__##fn(ctx, base);                                                    ResimWaitNote(addr, lr, T6NowMs() - t0);                                   }
T6_RESIM_WAIT(sub_824D3868, 0x824D3868u)
T6_RESIM_WAIT(sub_824D37D0, 0x824D37D0u)
T6_RESIM_WAIT(sub_824E0620, 0x824E0620u)
// sub_824DA2E0 = a D3D resolve (surface -> texture, then a wait for the GPU fence), reached once per frame through
// sub_8252FBF0 from the virtual sub_8253BDF8 (lr 0x8253BE34; the caller does not use the result). In a re-simulated
// frame of the fight nothing of it is shown: skipped there since 2.2.4 (rw1 2026-10-17: 0.2-0.3 ms of the ~1.9 ms of
// a re-simulated frame, all of it the GPU wait). But sub_8253BDF8 resolves one of two buffers (index this+20, 304 bytes
// each), and with no resolve in the last re-simulated frame the first live frame showed an old picture (user
// 2026-10-19: name plates and "VS BATTLE" twitch online "since the optimization"). 2.3.2 - 2.3.4 resolved only in the
// last re-simulated frame of a rollback: still broken pictures with a render scale (hud3 2026-10-20, scale 3, LagMs
// 120, frame captures: 3 whole frames of garbage + 15 name plate drop-outs in 70 s; none at all hud5: 36). Now every
// re-simulated frame resolves again, as before 2.2.4 (hud4 / hud6 / hud7: 0 broken frames at scale 3 / 1 / 2;
// re-simulated frame +0.16 ms at scale 1, +0.35 ms at scale 2).
// T6_RESIM_RESOLVE=last = only the last re-simulated frame (2.3.2 - 2.3.4), =0 = in none (2.2.4 - 2.3.1).
extern "C" void __imp__sub_824DA2E0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824DA2E0(PPCContext& ctx, uint8_t* base) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  static const bool only_last = T6EnvIs("T6_RESIM_RESOLVE", 'l'), none = T6EnvIs("T6_RESIM_RESOLVE", '0');
  const bool last = g_net_frame + 1 >= g_rb_end;
  if ((none || (only_last && !last)) && g_net_resim && t6_thread_bit == 0 && lr == 0x8253BE34u && T6GameMode() == 4) {
    ctx.r3.u64 = 0;
    ResimWaitNote(0x824DA2E0u, 0xFFFFFFFFu, 0);  // (counted as skipped)
    return;
  }
  const double t0 = T6NowMs();
  __imp__sub_824DA2E0(ctx, base);
  ResimWaitNote(0x824DA2E0u, lr, T6NowMs() - t0);
}
T6_RESIM_WAIT(sub_824E0C10, 0x824E0C10u)
#define T6_TIME_MAIN(fn, slot)                                             extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base);             extern "C" void fn(PPCContext& ctx, uint8_t* base) {                       if (t6_thread_bit != 0 || !g_net_running) {                                __imp__##fn(ctx, base);                                                  return;                                                                }                                                                        const double t0 = T6NowMs();                                               __imp__##fn(ctx, base);                                                  g_draw_ms[slot] += T6NowMs() - t0;  if (slot == 2) ResimWaitNote(0x824D3F08u, static_cast<uint32_t>(ctx.lr), T6NowMs() - t0);  }
T6_TIME_MAIN(sub_82218FA0, 0)
T6_TIME_MAIN(sub_82219098, 1)
T6_TIME_MAIN(sub_824D3F08, 2)
extern "C" void __imp__sub_8221CA68(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8221B7C0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8221CA68(PPCContext& ctx, uint8_t* base) {
  static const bool full = T6Env("T6_RESIM_DRAW") != nullptr;  // T6_RESIM_DRAW=1: full draw in re-simulated frames
  if (static const bool async = T6Env("T6_NET_AUDIO_ASYNC") != nullptr;
      async && g_net_running && !g_net_resim && t6_thread_bit == 0 && g_net_depth == 0) {
    T6AudioKick(g_net_frame + 1);
  }
  if (full || !(g_net_resim || g_net_skip_draw) || t6_thread_bit != 0 || T6GameMode() != 4) {
    T6PoseDiagDraw(true);
    __imp__sub_8221CA68(ctx, base);
    T6PoseDiagDraw(false);
    return;
  }
  const uint32_t self = ctx.r3.u32;
  *reinterpret_cast<uint32_t*>(base + self + 228) = _byteswap_ulong(1u);
  t6_dirty_mark(self + 228);
  constexpr uint32_t ref = 0x82735A74u;
  uint32_t obj = T6Rd32(ref + 16);
  if (obj && !(T6Rd32(obj + 12) == T6Rd32(ref + 20) && (T6Rd32(obj + 60) & T6Rd32(0x82007560u)) != 0)) obj = 0;
  ctx.r3.u64 = obj;
  __imp__sub_8221B7C0(ctx, base);
  ctx.r3.u64 = 0;
}

// (research, T6_CROWD_PAGES) at every frame boundary of its window: the pages the rest of the frame wrote; at the end
// the crowd's pages nobody else wrote
void T6CrowdPagesFrame() {
  static const uint32_t at = T6EnvU32("T6_CROWD_PAGES", 0, 0);
  if (!at || state().step < at || state().step > at + 3) return;
  if (state().step > at)
    for (uint32_t q = 0; q < sizeof(t6_dirty); q++)
      if (t6_dirty[q]) g_other_pages[q] = 1;
  std::memset(t6_dirty, 0, sizeof(t6_dirty));
  if (state().step != at + 3) return;
  std::string runs;
  uint32_t n = 0;
  for (auto it = g_crowd_pages.begin(); it != g_crowd_pages.end();) {
    if (g_other_pages.count(it->first)) { ++it; continue; }
    uint32_t a0 = it->first, e = a0 + 1;
    for (++it; it != g_crowd_pages.end() && it->first == e && !g_other_pages.count(e); ++it) e++;
    runs += fmt::format(" {:08X}-{:08X}", a0 << 12, e << 12);
    n += e - a0;
  }
  std::string shared;
  for (const auto& [q, v] : g_crowd_pages)
    if (g_other_pages.count(q)) shared += fmt::format(" {:08X}", q << 12);
  REXLOG_INFO("T6 crowd pages: {} written only by the crowd:{}", n, runs);
  REXLOG_INFO("T6 crowd pages: written by the crowd and others:{}", shared);
}

// The stage crowd's task objects (tcb_kkMob*, vtable at the node) in the task tree (root *(0x82828C60), links +16..+40)
std::vector<uint32_t> T6CrowdNodes() {
  const auto& kMob = kT6CrowdVt;
  std::vector<uint32_t> out, todo{T6Rd32(0x82828C60u)};
  std::unordered_set<uint32_t> seen;
  while (!todo.empty() && seen.size() < 20000) {
    const uint32_t n = todo.back();
    todo.pop_back();
    if (!T6HeapPtr(n) || !seen.insert(n).second) continue;
    const uint32_t v = T6Rd32(n);
    for (const uint32_t m : kMob)
      if (v == m) out.push_back(n);
    for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The stage crowd stays live through a load (user 2026-10-19, "B"; PS3 RB_RESIM_SKIP + crowd out of the snapshot): its
// tasks are not stepped in re-simulated frames, so a load that put their objects back made the whole crowd jump back
// by the rollback's frames at every rollback ("the crowd bugging out" on a stream, ~100 ms ping; cl0: the animation
// clock +0x8C went back in 12 % of the live frames). What stays live: the words of the crowd objects (0x2000 apart,
// cr1: 211 on Fiesta del Tomate) that change from frame to frame and never hold an address - the animation clock and
// the bone matrices. Learnt per crowd type (vtable) by comparing all objects 30 frames apart. Everything else is
// loaded as before: the task header (vtable, tree links, step, flags), and the addresses in the bodies (each 0xA0 bone
// block links into other structures - keeping the whole body live broke the task walk at the next frame, cl1).
// OFF by default since 2.3.3 (T6_CROWD_LIVE=1 = on): the fixed 0x2000 window reaches past small crowd objects into
// other tasks of the same heap - their counts / timers stayed live while their pointers were loaded: 2.3.2 crash in a
// match (user, stage 17, read of guest 0x298 in sub_8225A780 = ZSGEfctParticleBTcb, particle count != 0 with a null
// list - volum-fa). Needs the real object size before it can come back.
namespace {
bool CrowdAddress(uint32_t v) { return (v >= 0x82000000u && v < 0x82DB0000u) || (v >= 0xA7290000u && v < 0xABC00000u); }
}  // namespace
const std::vector<Range>& t6np::T6CrowdKeep() {
  static std::vector<Range> keep;
  static std::vector<uint32_t> nodes;
  static std::vector<std::vector<uint32_t>> first;  // the bodies at the start of the learning
  static uint32_t learn_at = 0, checked_at = 0;
  static bool learning = false;
  static const bool on = T6EnvIs("T6_CROWD_LIVE", '1');
  if (!on || g_net_resim) return keep;
  const uint32_t now = T6NetRunning() ? g_net_frame : state().step;
  constexpr uint32_t kWords = 0x2000 / 4;
  // the crowd of this stage: looked at again every 300 frames (a new stage in a rematch)
  if (!learning && (checked_at == 0 || now < checked_at || now - checked_at >= 300)) {
    checked_at = now;
    auto cur = T6CrowdNodes();
    cur.erase(std::remove_if(cur.begin(), cur.end(), [](uint32_t n) { return !T6InGameRange(n, 0x2000); }), cur.end());
    if (cur != nodes) {
      nodes = std::move(cur);
      keep.clear();
      first.assign(nodes.size(), std::vector<uint32_t>(kWords));
      for (size_t k = 0; k < nodes.size(); k++)
        for (uint32_t w = 0; w < kWords; w++) first[k][w] = T6Rd32(nodes[k] + 4 * w);
      learning = !nodes.empty();
      learn_at = now;
    }
  }
  if (learning && now != learn_at && now - learn_at >= 30) {
    learning = false;
    // per crowd type: changed in some object of the type, an address in none (neither then nor now)
    std::map<uint32_t, std::vector<uint8_t>> changed, address;  // vtable -> per word
    for (size_t k = 0; k < nodes.size(); k++) {
      const uint32_t vt = T6Rd32(nodes[k]);
      auto& c = changed[vt];
      auto& a = address[vt];
      c.resize(kWords);
      a.resize(kWords);
      for (uint32_t w = 0x80 / 4; w < kWords; w++) {
        const uint32_t v = T6Rd32(nodes[k] + 4 * w);
        if (v != first[k][w]) c[w] = 1;
        if (CrowdAddress(v) || CrowdAddress(first[k][w])) a[w] = 1;
      }
    }
    for (const uint32_t n : nodes) {
      const auto& c = changed[T6Rd32(n)];
      const auto& a = address[T6Rd32(n)];
      for (uint32_t w = 0x80 / 4; w < kWords;) {
        if (!c[w] || a[w]) { w++; continue; }
        uint32_t e = w + 1;
        while (e < kWords && c[e] && !a[e]) e++;
        keep.push_back({n + 4 * w, n + 4 * e});
        w = e;
      }
    }
    first.clear();
    REXLOG_INFO("T6 crowd: {} crowd objects, {} live ranges ({} KB) through loads", nodes.size(), keep.size(),
                [&] { uint64_t b = 0; for (const auto& r : keep) b += r.hi - r.lo; return b >> 10; }());
  }
  return keep;
}

// T6_CROWD_CLOCK=1 (test of the crowd fix): every live frame, the animation clock (+0x8C) of a crowd object in view;
// a clock that went back since the last live frame = the crowd jumped back. "T6 crowd clock" every 600 frames.
void T6CrowdClockTick() {
  static const bool on = T6Env("T6_CROWD_CLOCK") != nullptr;
  if (!on || !T6NetRunning() || T6GameMode() != 4) return;
  static uint32_t node = 0, frames = 0, back = 0, still = 0, checked = 0;
  static float last = 0;
  if (!node || frames % 600 == 0) {
    const auto nodes = T6CrowdNodes();
    node = nodes.empty() ? 0 : nodes[nodes.size() / 2];
    last = 0;
  }
  frames++;
  if (!node) return;
  const uint32_t u = T6Rd32(node + 0x8C);
  float c;
  std::memcpy(&c, &u, 4);
  if (last != 0) {
    checked++;
    if (c < last) back++;
    else if (c == last) still++;
  }
  last = c;
  if (frames % 600 == 0)
    REXLOG_INFO("T6 crowd clock: {} live frames checked, {} back, {} standing still (rollback loads {})", checked, back, still, g_net_loads);
}
