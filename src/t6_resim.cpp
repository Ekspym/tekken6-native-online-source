// Tekken 6 netplay - re-simulated frames: task profile, skip list, draw root (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Task step profile of re-simulated frames (PS3 RB_TASK_PROFILE) ----
// sub_822188C8(node, r4) = one task step of the walker sub_82218C80 (PS3 FUN_000ff344): r = (*(node+48))(node), then
// the next node. T6_TASK_PROFILE=1: time per step function / first word of the node in re-simulated frames (and the
// same for live frames), logged every 100 re-simulated frames -> which tasks a re-simulated frame pays for.
extern "C" void __imp__sub_822188C8(PPCContext& ctx, uint8_t* base);
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
// frame of the fight nothing of it is shown and the next live frame resolves again: not done there (rw1 2026-10-17:
// 0.2-0.3 ms of the ~1.9 ms of a re-simulated frame, all of it the GPU wait). T6_RESIM_RESOLVE=1 = always done.
extern "C" void __imp__sub_824DA2E0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824DA2E0(PPCContext& ctx, uint8_t* base) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  static const bool keep = T6Env("T6_RESIM_RESOLVE") != nullptr;
  if (!keep && g_net_resim && t6_thread_bit == 0 && lr == 0x8253BE34u && T6GameMode() == 4) {
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
    __imp__sub_8221CA68(ctx, base);
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
