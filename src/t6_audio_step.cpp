// Tekken 6 netplay - CnuSound tick thread in step with the frames, audio step in the draw (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- CnuSound2 tick thread in step with the game frames ----
// The library's own thread (sub_82580038) loops in real time: every 5 ms it measures the elapsed GetTickCount time and
// calls sub_8257EF30(object, elapsed ms) for each of the *(0x82CE4BA0) objects at 0x82CE4BA8 (2428 bytes each), until
// the run flag *(0x82CE9938) is cleared. Stream / fade / voice state therefore advanced by wall clock and the sound
// queries of the game (finished? playing?) differed between the instances now and then (m3: from frame ~210; v24/v25).
// In a session the loop waits for a budget instead: T6NetFrame grants one pass with the frame's 16 / 17 ms before every
// live frame and waits until it ran (like rex_audio_advance for the XAudio callbacks).
extern "C" void __imp__sub_82580038(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8257EF30(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_824BE068(PPCContext& ctx, uint8_t* base);  // Sleep(ms)
namespace t6np {
std::mutex g_cnu_mutex;
std::condition_variable g_cnu_cv;
bool g_cnu_lockstep = false;
std::deque<uint32_t> g_cnu_budget;  // elapsed ms of the granted passes
uint64_t g_cnu_done = 0, g_cnu_granted = 0;
}  // namespace t6np
void T6CnuLockstep(bool on) {
  {
    std::lock_guard lock(g_cnu_mutex);
    g_cnu_lockstep = on;
    g_cnu_budget.clear();
    g_cnu_granted = g_cnu_done;
  }
  g_cnu_cv.notify_all();
}
bool T6CnuAdvance(uint32_t ms, int timeout_ms) {
  std::unique_lock lock(g_cnu_mutex);
  if (!g_cnu_lockstep) return true;
  g_cnu_budget.push_back(ms);
  g_cnu_granted++;
  g_cnu_cv.notify_all();
  return g_cnu_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] { return g_cnu_done >= g_cnu_granted; });
}
extern "C" void sub_82580038(PPCContext& ctx, uint8_t* base) {
  if (!net_config().enabled) {
    __imp__sub_82580038(ctx, base);
    return;
  }
  const auto pass = [&](uint32_t ms) {
    const int32_t n = static_cast<int32_t>(T6Rd32(0x82CE4BA0u));
    for (int32_t i = 0; i < n; i++) {
      ctx.r3.u64 = 0x82CE4BA8u + static_cast<uint32_t>(i) * 2428u;
      ctx.r4.u64 = ms;
      __imp__sub_8257EF30(ctx, base);
    }
  };
  REXLOG_INFO("T6 net: CnuSound tick thread {}", GetCurrentThreadId());
  DWORD last = GetTickCount();
  while (T6Rd32(0x82CE9938u) != 0) {
    uint32_t ms = 0;
    bool run = false, locked = false;
    {
      std::unique_lock lock(g_cnu_mutex);
      locked = g_cnu_lockstep;
      if (locked) {
        g_cnu_cv.wait_for(lock, std::chrono::milliseconds(50), [] { return !g_cnu_lockstep || !g_cnu_budget.empty(); });
        if (g_cnu_lockstep && !g_cnu_budget.empty()) {
          ms = g_cnu_budget.front();
          g_cnu_budget.pop_front();
          run = true;
        }
      }
    }
    if (locked) {
      if (run) {
        pass(ms);
        {
          std::lock_guard lock(g_cnu_mutex);
          g_cnu_done++;
        }
        g_cnu_cv.notify_all();
      }
      last = GetTickCount();
      continue;
    }
    // real time, as the original loop
    const DWORD now = GetTickCount();
    pass(now - last);
    last = now;
    ctx.r3.u64 = 5;
    __imp__sub_824BE068(ctx, base);
  }
  ctx.r3.u64 = 0;
  // the original ends with ExitThread(0) (sub_824BD590); returning from the thread procedure does the same
}

// Sound in step with the game frames FROM BOOT (T6_NET): the XAudio callbacks of a frame (48000 / 60 = 800 samples =
// 3.125 frames of 256) and one CnuSound tick (16 / 17 ms) run at the frame boundary, before the frame. Only from the
// session start it was not enough: a sound started in the menus (real time audio) was at another position on each
// instance and its "finished" query flipped one frame apart (m5: frame 98 vs 99) -> another intro on each side.
// Until the audio system / the Cnu thread exist the budget is dropped again (nothing would consume it).
void AudioStep(uint64_t f, bool in_session) {
  const auto lock = T6Rex().audio_lockstep;
  const auto adv = T6Rex().audio_advance;
  static bool on = false, audio_alive = false, cnu_alive = false;
  if (!lock || !adv) return;
  {
    const auto fast = T6Rex().audio_fast;
    static const bool fastboot = T6EnvDefaultOn("T6_NET_FASTBOOT");
    static int state_now = -1;
    const int want = (fastboot && !in_session && !g_net_fast_over) ? 1 : 0;
    if (fast && want != state_now) {
      state_now = want;
      fast(want);
      if (want) rex::cvar::SetFlagByName("vsync", "false");
      REXLOG_INFO("T6 net: fast start {} at pad step {}", want ? "on (no sound, no 60 per second pace)" : "off", state().step);
    }
  }
  if (!on) {
    on = true;
    lock(1);
    T6CnuLockstep(true);
    REXLOG_INFO("T6 net: audio lockstep on (from pad step {})", state().step);
  }
  const uint32_t n = static_cast<uint32_t>((f + 1) * 800 / 256 - f * 800 / 256);
  // The audio callback only signals the XAudio2 processing thread (sub_824F0250, event 0x82984BF0), which then mixes /
  // retires voices on its own: it ran on while the next frame queried the voices -> "sound finished" a frame apart
  // (another intro camera on each instance from frame ~210). Wait until it is back on its event, nothing pending.
  using WaitersFn = int (*)(uint32_t);
  const auto entries = T6Rex().wait_entries;
  const auto waiters = T6Rex().waiters;
  constexpr uint32_t kXAudioEvent = 0x82984BF0u;
  const uint64_t e0 = entries ? entries(kXAudioEvent) : 0;
  const double a0 = T6NowMs();
  struct AudioParts {
    double a0, a1 = 0, a2 = 0;
    bool session;
    ~AudioParts() {
      if (!session) return;
      const double a3 = T6NowMs();
      g_audio_parts[0] += a1 - a0;
      g_audio_parts[1] += a2 - a1;
      g_audio_parts[2] += a3 - a2;
    }
  } parts{a0, a0, a0, in_session};
  const bool adv_ok = adv(n, audio_alive ? 250 : 2);
  parts.a1 = parts.a2 = T6NowMs();
  if (adv_ok) {
    audio_alive = true;
    if (entries && waiters && e0 > 0) {
      const double t0 = T6NowMs();
      while (!(entries(kXAudioEvent) > e0 && waiters(kXAudioEvent) >= 1 && T6Rd32(kXAudioEvent + 4) == 0)) {
        if (T6NowMs() - t0 > 100) {
          static int warned = 0;
          if (warned++ < 10) REXLOG_WARN("T6 net: {} {}: XAudio thread not idle after 100 ms", in_session ? "frame" : "step", f);
          break;
        }
        Sleep(0);
      }
    }
  } else if (!audio_alive) {
    lock(1);  // no audio client yet: drop the budget
  } else {
    REXLOG_WARN("T6 net: {} {}: audio callbacks not done after 250 ms", in_session ? "frame" : "step", f);
  }
  parts.a2 = T6NowMs();
  const uint32_t ms = static_cast<uint32_t>((f + 1) * 1000 / 60 - f * 1000 / 60);
  if (T6CnuAdvance(ms, cnu_alive ? 250 : 2)) {
    cnu_alive = true;
  } else if (!cnu_alive) {
    T6CnuLockstep(true);  // thread not running yet: drop the budget
  } else {
    REXLOG_WARN("T6 net: {} {}: CnuSound tick not done after 250 ms", in_session ? "frame" : "step", f);
  }
}


// ---- Audio step in the draw (reserve of the live frame; user 2026-10-11: no effects removed, win the time elsewhere) ----
// The audio step of a frame (XAudio callbacks for 800 samples + one CnuSound tick, ~3 ms) is work of OTHER threads the
// main thread only waits for at the frame boundary. T6_NET_AUDIO_ASYNC=1: it is started when the draw root of the
// live frame begins (the game logic of the frame, with its sound calls, is done by then) and runs while the main
// thread builds the frame's draw commands (~5 ms); the boundary only joins it. The order stays the same on both
// instances: a sound call of the main thread that comes after the start first waits for the step to finish
// (t6_snd_call), and the save / load of the boundary come after the join as before. Not in re-simulated frames (they
// have no audio step) and not outside a session.
namespace t6np {
struct AudioAsync {
  std::mutex m;
  std::condition_variable cv;
  bool has = false, busy = false, started = false;
  uint64_t f = 0, kicked_f = ~0ull;
} g_audio_async;
uint64_t g_audio_async_n = 0, g_audio_async_joins_in_calls = 0;
}  // namespace t6np
void T6AudioKick(uint64_t f) {
  auto& a = g_audio_async;
  std::unique_lock lock(a.m);
  if (a.busy || a.has || a.kicked_f == f) return;
  if (!a.started) {
    a.started = true;
    std::thread([] {
      auto& a = g_audio_async;
      for (;;) {
        uint64_t f = 0;
        {
          std::unique_lock lock(a.m);
          a.cv.wait(lock, [&] { return a.has; });
          a.has = false;
          f = a.f;
        }
        AudioStep(f, true);
        {
          std::lock_guard lock(a.m);
          a.busy = false;
        }
        a.cv.notify_all();
      }
    }).detach();
  }
  a.f = a.kicked_f = f;
  a.has = a.busy = true;
  g_audio_async_n++;
  lock.unlock();
  a.cv.notify_all();
}
void T6AudioJoin() {
  auto& a = g_audio_async;
  std::unique_lock lock(a.m);
  a.cv.wait(lock, [&a] { return !a.busy; });
}
// boundary: the step of frame f - already started in the draw (join) or run here as before
void AudioStepOrJoin(uint64_t f) {
  bool kicked = false;
  {
    std::lock_guard lock(g_audio_async.m);
    kicked = g_audio_async.kicked_f == f;
  }
  if (kicked) T6AudioJoin();
  else {
    T6AudioJoin();  // (nothing may be running; a stale start is finished first)
    AudioStep(f, true);
  }
}

double g_phase_shift_ms = 0;  // balancer / alignment: the frames still have to move this much later (frame clock)
double g_clock_pushed_sum = 0;  // (statistics: ms the clock was moved for input waits in this window)
double g_clock_audio_frames = 0;  // audio frames the device played out of its queue during those waits
double g_clock_push_ms = 0;   // time this side waited for the peer's input: the frame clock moves that much later
double g_phase_audio_ms = 0;  // ... the part of it the audio device gets extra frames for (5.33 ms each, rex_audio_delay)
double g_depth_target = 4.0;  // audio queue depth the frame clock holds (a half-frame phase shift lowers it by 0.5)
double g_depth_sum = 0;       // window sum of the averaged audio queue depth
double g_ft_sleep = 0, g_ft_slack_sum = 0;  // pacing wait of this frame / of the window (PS3 "Frame end wait" = slack)
int g_ft_late = 0;                          // frames of the window that reached the pacing point after their deadline
double g_ft_last = 0, g_ft_period_sum = 0, g_ft_period_max = 0, g_ft_bound_sum = 0, g_ft_bound_max = 0;
int g_ft_over17 = 0, g_ft_over25 = 0, g_ft_bound_over8 = 0;
double g_bp_sum[6] = {}, g_ft_out = 0, g_ft_game_sum = 0;  // window sums: boundary parts, game frame (boundary end -> next boundary)
double g_bp[8] = {}, g_bp_max[8] = {};
// Below 60 FPS (user 2026-10-15: a player whose game cannot hold 60 frames per second lags the other one; more than
// 30 % such frames in a game = no rematch, the set ends): a fight frame (game mode 4) is SLOW when its period was over
// 17.5 ms and the boundary before it waited for nothing (pacing wait + input wait under 1 ms) - this
// game itself was the brake, never the peer (waiting for him is input wait). Counted per game (reset by the loading
// and the intros of the next one); the status file shows "slow <percent>" with the winner (t6_status.cpp).
uint32_t g_slow_frames = 0, g_fight_frames = 0;
// The opponent's game lagging behind (user 2026-10-15, a player vs a Kazuya: the peer 5-9 frames behind for the
// whole match, every live frame of this side with a predicted remote input, rollbacks of 3-5 frames all the time;
// the peer was not slow enough for the FPS limit): live fight frames whose remote input was predicted, and the
// peer's frames behind (GGPO time sync) sampled every 15 fight frames. Reset with the slow frames.
uint32_t g_lag_pred = 0, g_behind_sum = 0, g_behind_n = 0, g_ping_sum = 0;  // (g_ping_sum: ms, same samples)
double g_last_waits = 99.0;  // boundary parts of this frame / of the longest boundary of the window
// 'park': the session of the finished match is closed in its result menu and the game walks back to the hold point
// (pad reads of g_park). wait_steps: pad steps the walk starts later (the menu has not stood its 60 frames yet).
void ParkSession(uint32_t wait_steps) {
  g_park_wanted = false;
  g_menu_frames = 0;
  // Safety net for the XMA decoder error ("There are no bits to copy", two players 2026-10-12): after it the sound
  // dies bit by bit and the next match hangs in its loading. A game that saw it does not park: it ends here and the
  // launcher starts a fresh one (as after a closed game) - ~11 s instead of a broken next match.
  {
    const auto xma_errors = T6Rex().audio_xma_errors;
    if (xma_errors && xma_errors() > 0) {
      REXLOG_WARN("T6 park: {} XMA decoder error(s) in this run - the game restarts instead of parking", xma_errors());
      ExitProcess(0);
    }
  }
  REXLOG_INFO("T6 park: session closed at frame {} (result menu), pad step {}", g_net_frame, state().step);
  T6AudioJoin();
  ggpo_close_session(g_ggpo);
  g_ggpo = nullptr;
  g_net_running = g_net_started = g_net_valid = g_net_pending = false;
  g_park = true;
  g_park_held = g_park_started = g_park_no_battle = g_park_new_battle = false;
  g_park_step0 = state().step + wait_steps;
  g_park_t1 = 0;
  T6StatusReset();
  g_park_downs = (2 - g_result_cursor + 4) % 4;
  g_result_cursor = 2;
  REXLOG_INFO("T6 park: {} down presses to CHARACTER SELECT", g_park_downs);
  g_watch_pause = true;
  g_net_fast_over = false;  // the way back to the hold point is not shown: no sound, no 60 per second pace
}

// Frame boundary in a session (true: handled, the snapshot test is off)
