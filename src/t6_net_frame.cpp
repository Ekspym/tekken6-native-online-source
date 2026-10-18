// Tekken 6 netplay - the session frame (T6NetFrame) (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

bool T6NetFrame() {
  const NetConfig& c = net_config();
  if (!c.enabled || !g_main_ctx) return false;
  if (g_net_depth > 0) return true;
  if (!g_net_started) {
    // Deterministic loading from boot (the replay up to the session must reach the fight loading at the same pad step
    // on both instances: v4 started at 2843 / 2846 -> different state, different intros)
    if (state().step > 0 && !WaitLoaderIdle(5000)) REXLOG_WARN("T6 net: step {}: file loader still busy after 5 s", state().step);
    static uint64_t boundaries = 0;  // main loop iterations before the session
    AudioStep(boundaries++, false);
    // T6_PARK_TRACE=1 (diagnostics): the task types that appear / disappear on the way back to the hold point and
    // from the start command to the battle, by pad step - which screen exists when
    if (static const bool trace = T6Env("T6_PARK_TRACE") != nullptr; trace && g_park) {
      static std::vector<uint32_t> last;
      const std::vector<uint32_t> now = TaskTreeVtables();
      if (now != last) {
        std::string add, gone;
        for (const uint32_t v : now) if (!std::binary_search(last.begin(), last.end(), v)) add += fmt::format(" {:08X}", v);
        for (const uint32_t v : last) if (!std::binary_search(now.begin(), now.end(), v)) gone += fmt::format(" {:08X}", v);
        REXLOG_INFO("T6 park trace: step {} (+{}): {} task types; new:{}; gone:{}", state().step, static_cast<int32_t>(state().step - g_park_step0),
                    now.size(), add, gone);
        last = now;
      }
    }
    if (g_park && !g_park_started) {
      const bool battle = TaskTreeHas(0x8200C5E8u, 0x8200C5E8u);
      static uint32_t last_mode = ~0u;
      const uint32_t gm = T6GameMode();
      if (gm != last_mode) {
        REXLOG_INFO("T6 park: step {} (+{}): game mode {} -> {}, battle task {}", state().step, state().step - g_park_step0, last_mode, gm, battle);
        last_mode = gm;
      }
      if (!battle && !g_park_no_battle) {
        g_park_no_battle = true;
        g_park_t1 = state().step;
        REXLOG_INFO("T6 park: step {} (+{}): the battle task is gone", state().step, state().step - g_park_step0);
      }
      if (battle && g_park_no_battle && !g_park_new_battle) {
        g_park_new_battle = true;
        REXLOG_INFO("T6 park: step {} (+{}): a new battle task", state().step, state().step - g_park_step0);
      }
      return true;
    }
    if (!TaskTreeHas(0x8200C5E8u, 0x8200C5E8u)) return true;
    g_net_started = true;
    g_net_fast_over = true;
    if (!StartSession()) return true;
  }
  if (!g_ggpo || !g_net_running) return true;
  {
    static const int autopark = T6EnvInt("T6_NET_AUTOPARK", 0);
    if (T6StatusTakePark()) g_park_wanted = true;
    g_menu_frames = T6GameMode() == 27 ? g_menu_frames + 1 : 0;
    // (the menu must stand for a moment before it takes input: 60 frames)
    if ((g_park_wanted && g_menu_frames >= 60) || (autopark > 0 && g_menu_frames == static_cast<uint32_t>(autopark))) {
      ParkSession(0);
      return true;
    }
  }
  // Hang watch of the session: live + re-simulated frames + loads must keep advancing (pc4: an instance froze after a
  // load with nothing in the log)
  static uint32_t progress = 0;
  progress = static_cast<uint32_t>(g_net_frames + g_net_resim_frames + g_net_loads);
  StartHangWatch(&progress);
  g_main_phase = 0;
  // (voices orphaned by this boundary's rollback are released at its end, after the re-simulation: a replayed sound
  // call puts its voice back into the entry, vl4)
  struct Where {
    ~Where() noexcept {
      try {
        if (g_in_rollback) T6VoicesAfterLoad();
        T6OrphansTick();
        T6LatMoveLog();
      } catch (...) {  // (a destructor does not throw)
        T6SwallowedExceptions()++;
      }
      g_net_where = "game frame";
      g_main_phase = 1;
      g_in_rollback = false;
    }
  } where_guard;
  if (static const bool smp = T6Env("T6_NET_SAMPLE") != nullptr; smp) {
    T6SampleStart();
    T6SampleDump(g_net_frame);
  }
  g_net_depth++;
  // Frame time statistics (user target: no frame over 16.7 ms): period = boundary to boundary (with the vsync wait),
  // boundary = time spent here (audio step, loader wait, GGPO, save, rollback + re-simulation)
  struct FrameTimer {
    double t_in = T6NowMs();
    uint64_t resim0 = g_net_resim_frames;
    double load0 = g_load_ms;
    FrameTimer() {
      if (g_ft_out > 0) g_ft_game_sum += t_in - g_ft_out;
      {
        const uint32_t gm = T6GameMode();
        if ((gm == 14 || gm == 0) && g_fight_frames) g_slow_frames = g_fight_frames = g_lag_pred = g_behind_sum = g_behind_n = g_ping_sum = 0;
        if (gm == 4 && g_ft_last > 0) {
          g_fight_frames++;
          g_slow_frames += t_in - g_ft_last > 17.5 && g_last_waits < 1.0;
          g_lag_pred += g_ggpo_predicted != 0;
          if (g_fight_frames % 15 == 0 && g_ggpo) {
            GGPONetworkStats st{};
            if (GGPO_SUCCEEDED(ggpo_get_network_stats(g_ggpo, g_net_remote, &st))) {
              g_behind_sum += static_cast<uint32_t>(std::max(0, st.timesync.remote_frames_behind));
              g_ping_sum += static_cast<uint32_t>(std::max(0, st.network.ping));
              g_behind_n++;
            }
          }
        }
      }
      if (g_ft_last > 0) {
        const double p = t_in - g_ft_last;
        g_ft_period_sum += p;
        g_ft_period_max = std::max(g_ft_period_max, p);
        g_ft_over17 += p > 17.5;
        g_ft_over25 += p > 25.0;
      }
      g_ft_last = t_in;
      // (tests) T6_TEST_SLOW_MS=<ms>: that much extra work in every fight frame - a PC too slow for 60 FPS
      if (static const int slow_ms = T6EnvInt("T6_TEST_SLOW_MS", 0); slow_ms > 0 && T6GameMode() == 4)
        Sleep(static_cast<DWORD>(slow_ms));
    }
    ~FrameTimer() {
      const double b = T6NowMs() - t_in - g_ft_sleep;  // work of the boundary, without the pacing wait
      g_last_waits = g_ft_sleep + g_bp[2];  // (pacing wait + input wait; the audio step mixes ~2 ms even when late: see g_slow_frames)
      g_ft_sleep = 0;
      g_ft_bound_sum += b;
      if (b > g_ft_bound_max) {
        std::memcpy(g_bp_max, g_bp, sizeof(g_bp));
        g_bp_max[6] = static_cast<double>(g_net_resim_frames - resim0);
        g_bp_max[7] = g_load_ms - load0;
      }
      for (int k = 0; k < 6; k++) g_bp_sum[k] += g_bp[k];
      std::memset(g_bp, 0, sizeof(g_bp));
      g_ft_out = T6NowMs();
      g_ft_bound_max = std::max(g_ft_bound_max, b);
      g_ft_bound_over8 += b > 8.0;
    }
  } frame_timer;
  // The live frame that ran since the last boundary is done: GGPO saves this boundary (and may roll back)
  // Order of the boundary (T6_NET_AUDIO_LATE=1 = the old order: save first). The save has to wait until the draw
  // worker finished its CPU work of the frame that just ran (~1.9 ms: it is still building D3D commands, T6_NET_WORKER_DIAG).
  // The sound step of the next frame (audio callbacks + CnuSound tick, ~2.5 ms) and the loader wait do not touch the
  // draw worker, so they run first and the two waits overlap. (PS3 moved its draw wait the same way: "the main thread
  // waits for the same condition later in the frame anyway".)
  static const bool audio_first = T6Env("T6_NET_AUDIO_LATE") == nullptr;
  bool audio_done = false;
  if (g_net_pending && audio_first) {
    const double u0 = T6NowMs();
    g_net_where = "audio step";
    AudioStepOrJoin(g_net_frame + 1);
    g_bp[4] = T6NowMs() - u0;
    const double l0 = T6NowMs();
    g_net_where = "loader wait";
    if (!WaitLoaderIdle(5000)) REXLOG_WARN("T6 net: frame {}: file loader still busy after 5 s", g_net_frame + 1);
    g_loader_wait_ms += T6NowMs() - l0;
    g_bp[5] = T6NowMs() - l0;
    audio_done = true;
  }
  if (g_net_pending) {
    g_net_pending = false;
    g_net_frame++;
    const double a0 = T6NowMs();
    g_net_where = "advance";
    ggpo_advance_frame(g_ggpo);
    g_bp[0] = T6NowMs() - a0;
    g_net_where = "after advance";
  }
  // Frame pacing of the session (60 per second). The wait sits after the save and the rollback and before the inputs
  // are received and read (PS3: the rollback runs in the reserve of the frame, the inputs are taken as late as
  // possible): a rollback shorter than the reserve costs no frame, and a remote input that arrives during the wait is
  // used by this frame instead of rolling it back. A late frame is not waited for; behind by more than 50 ms the
  // schedule starts again from now (no burst of fast frames).
  // The frame clock (2026-10-11, ph1: 53-62 % of the frames over 17.5 ms with a correct average). The audio device
  // takes the game's sound in bursts of ~2 audio frames every ~10.7 ms, and while the device queue was full the
  // audio step of a frame waited for those bursts: frame periods jumped between ~11, ~16 and ~21 ms. Now the queue
  // is kept around half full (the audio step does not wait) and the frames follow this clock: 60 per second, the
  // period corrected by at most 0.25 ms by the averaged queue depth (the two clocks drift; target 4 of 8 frames,
  // ~21 ms of sound in reserve). A phase shift of the balancer = one longer period + one extra audio frame.
  if (NetOwnPace()) {
    static double next = 0, depth_avg = -1;
    const auto queued = T6Rex().audio_queued;
    const double now = T6NowMs();
    // (the time waited for the peer's input is taken off first, see below: it is not "being behind")
    static const bool push_on = T6EnvDefaultOn("T6_NET_CLOCK_PUSH");
    const double push = push_on && next != 0 ? g_clock_push_ms : 0;
    next += push;
    if (next == 0 || now > next + 50.0) next = now;
    // A phase shift is spread over the following frames, T6_NET_PHASE_STEP ms per frame (default 1.0; 0 = all at once
    // as before). It used to be one long frame: 10.7 ms on top of 16.7, once per 10 s of fight in the user's log of
    // 2026-10-12 (113 shifts in 18.6 minutes) = a visible hitch each time (a player: "the frame pacing seems bad").
    // Now 11 frames of 17.7 ms. The audio device gets its extra frames as the shift goes, one per 5.33 ms moved (two
    // at once could fill its queue: the audio step then waits).
    // A wait for the peer's input (prediction window full in the fight, lockstep outside it) means this side runs
    // ahead of what the connection carries. The frame clock used to keep its schedule, so the frames after the wait ran
    // without a pause until the lead was back - and the next frame waited again: the user's log of 2026-10-12, 73 % of
    // all late frames of the fights sit in 17 windows like "101 of 120 frames late, input wait up to 42 ms", 8-10 s in
    // a row, until the balancer had moved the side 10.7 ms at a time (a hitch of the PEER leaves it behind for good:
    // its clock starts again after 50 ms). Now the clock takes the waited time at once: the lead is gone with the one
    // wait that showed it. The audio device gets the frames for that time. T6_NET_CLOCK_PUSH=0 = off.
    static double owed = 0;  // extra audio frames (5.33 ms each) the device still gets, one per game frame
    // (The push is added BEFORE the "more than 50 ms behind -> start again from now" check above: added after it, a
    // wait over 50 ms - the seconds both sides wait for each other at the session start - was counted twice and the
    // next frame stood that long again: hang watch "main thread in 'pacing wait'", vo2 2026-10-13.)
    if (g_clock_push_ms > 0) {
      if (push > 0) {
        // (what the device really played out of its queue during the wait, not wait / 5.33: a long wait empties
        // the queue, and frames for the whole wait would fill it to its 8 - a full queue makes the audio step wait)
        owed += g_clock_audio_frames;
        g_clock_pushed_sum += push;
      }
      g_clock_push_ms = g_clock_audio_frames = 0;
    }
    if (g_phase_shift_ms > 0) {
      static const double cap = T6EnvDouble("T6_NET_PHASE_STEP", 1.0);
      const double step = cap > 0 ? std::min(g_phase_shift_ms, cap) : g_phase_shift_ms;
      next += step;
      g_phase_shift_ms -= step;
      const double pay = std::min(step, g_phase_audio_ms);
      g_phase_audio_ms -= pay;
      owed += pay / (256000.0 / 48000.0);
    }
    if (owed >= 0.999) {
      const auto delay = T6Rex().audio_delay;
      if (delay) delay(1);
      owed -= 1.0;
    }
    g_ft_late += now > next;
    // Catch-up (PS3: "a late frame is not waited for" + its lost image per rollback): a live frame of the fight that
    // starts more than 3 ms behind its deadline is not drawn (the draw root runs like in a re-simulated frame: film
    // only, ~5 ms of 10 saved), never two frames in a row (30 images per second at worst). The game keeps its 60 frames per second and gives up an
    // image instead of slowing down, which also keeps the two sides in step. T6_NET_FRAMESKIP=0 = always draw.
    {
      // OFF by default (T6_NET_FRAMESKIP=1 = on): rb1 / rb2 - "late" by this schedule was every frame, so every
      // other frame went undrawn for nothing. The frames are really paced by the audio lockstep (the audio step
      // blocks until the sound device has taken its 800 samples; the reserve of a frame shows up as the "audio"
      // time of the frame composition line, not as a pacing wait here), and this schedule sits a few ms before it.
      // To be redone on the audio clock (room in the device queue) if images have to be dropped at all.
      static const bool skip_on = T6EnvIs("T6_NET_FRAMESKIP", '1');
      static int run = 0;
      // (rb1, 2 in a row allowed: with no reserve in the frame 80 of 120 frames went undrawn = 20 images per second)
      const bool skip = skip_on && now > next + 3.0 && run < 1 && T6GameMode() == 4;
      run = skip ? run + 1 : 0;
      g_net_skip_draw = skip;
      g_ft_skipped += skip;
    }
    g_net_where = "pacing wait";
    // sleep most of the wait, spin the last 1.5 ms; while sleeping the pad is read about every millisecond (LocalPoll)
    while (true) {
      const double left = next - T6NowMs();
      if (left <= 0) break;
      if (left > 2.5 && LatchOn()) { Sleep(1); LocalPoll(); }
      else if (left > 2.0) Sleep(static_cast<DWORD>(left - 1.5));
      else YieldProcessor();
    }
    g_ft_sleep = T6NowMs() - now;
    g_ft_slack_sum += g_ft_sleep;
    double period = 1000.0 / 60.0;
    if (queued) {
      const int q = queued();
      depth_avg = depth_avg < 0 ? q : depth_avg * 0.98 + q * 0.02;
      period += std::clamp((depth_avg - g_depth_target) * 0.08, -0.25, 0.25);
      g_depth_sum += depth_avg;
    }
    next += period;
  }
  // PS3: intros / KO / replays ran in lockstep (no prediction): a rolled back intro choice (held button) or round start
  // went wrong. The first T6_NET_LOCKSTEP frames of the session (round intro, default 600) wait for confirmed inputs.
  // (240 since 2026-10-12, was 900: with skipped intros - START of a player or 'skipintro' - the fight began before
  // frame 900 and its first seconds ran in lockstep with the lockstep input delay of 4 frames. The frame count only
  // covers the loading, where the game mode of the last match may still stand; after it the game mode decides.)
  static const uint32_t lockstep = T6EnvU32("T6_NET_LOCKSTEP", 240, 0);
  int& mode = g_net_mode;
  // ... and every frame outside the fight proper (game mode != 4: loading, intros, round call, KO, replay, between
  // rounds, result, the menu after the match and the loading of a rematch), whenever it comes in the session.
  const int want = (g_net_frame < lockstep || T6GameMode() != 4) ? 1 : -1;
  // KO without the hitch (user 2026-10-15: "aby hra nelagla pri KO"): the switch fight -> lockstep at the KO set the
  // longer input delay and prediction 1 in the same frame, so that frame waited for the peer's input of the frames
  // just predicted - one frame of 25-36 ms at 30-60 ms ping, 50-75 ms at 80-110 ms (players' logs 2026-10-15: the
  // window after each KO, its longest frame = input wait 10-20 / 45-65 ms). Now the delay goes up at once (the filler
  // copies of the last input reach the peer with this frame) and the prediction stays for T6_NET_KO_GRACE frames
  // (default 8; 0 = old): by then the peer's fillers are here and lockstep needs no wait. The predictions of those
  // frames are the peer's last input = its fillers, so they rarely roll back; a rollback there runs the full frame
  // like any rollback after the KO (re-simulated frames of mode != 4 draw in full).
  static const int ko_grace = T6EnvInt("T6_NET_KO_GRACE", 8);
  static uint32_t grace_until = 0;  // frame from which the prediction of a lockstep phase is 1
  if (want != mode) {
    const bool ko = mode == -1 && want == 1;  // fight -> lockstep (KO, end of the round); not the session start
    mode = want;
    grace_until = ko ? g_net_frame + ko_grace : 0;
    g_net_max_pred = mode == 1 && grace_until > g_net_frame ? -1 : mode;
    ggpo_set_max_prediction(g_net_max_pred);  // (-1 = the full prediction window)
    // Lockstep needs the remote input of a frame before the frame runs: with delay 1 that fails from ~17 ms one way
    // (50 ms ping: the round intro ran at 30 fps). The lockstep phase uses a longer input delay (T6_NET_LOCKSTEP_DELAY,
    // default 4 frames = up to ~50 ms one way), the fight goes back to the chosen delay (GGPO drops the local inputs
    // of the frames in between, at the round call).
    static const int ls_delay = T6EnvInt("T6_NET_LOCKSTEP_DELAY", 4);
    SetLocalDelay(mode == 1 ? std::max(net_config().delay, ls_delay) : net_config().delay);
    REXLOG_INFO("T6 net: frame {}: {}", g_net_frame, mode == 1 ? (ko ? "lockstep (round intro) after a grace of prediction" : "lockstep (round intro)") : "rollback, prediction 6");
  } else if (mode == 1 && grace_until && g_net_frame >= grace_until) {
    grace_until = 0;
    g_net_max_pred = 1;
    ggpo_set_max_prediction(1);
  }
  // round start (Versus rec_vs: fight control from about session frame 770; players still at their start positions)
  // ONLY with T6_NET_FIND_POS=1 (diagnostics). The search walks 75 MB of the game heaps (30-45 ms) and ran every 10
  // frames from session frame 600 to 900 until it found something - in real matches it never did ("fight positions
  // not found yet"), so EVERY match had 31 frames of 45-60 ms through the round call and the first seconds of round 1:
  // the stutter the user reported since v2.0.1 (his logs 2026-10-12: over 25 ms 12 per 120 frames, exactly every
  // 10th frame, boundary max 30-56 ms with nothing in its parts, in all sessions, also with 0 ms of input wait).
  // The columns it fills in fight_state.csv are start markers, not live positions; the desync check is the players hash.
  if (static const bool find_pos = T6Env("T6_NET_FIND_POS") != nullptr;
      find_pos && !g_pos_addr && g_net_frame >= 600 && g_net_frame <= 900 && g_net_frame % 10 == 0) FindFightPositions();
  // T6_NET_HEAPDUMP=1: raw game heaps at session frames 780 and 1200 -> <dump dir>/heap_<frame>.bin (player 1 only)
  if (T6Env("T6_NET_HEAPDUMP") && net_config().player == 1 && (g_net_frame == 780 || g_net_frame == 1200)) {
    const char* d = T6Env("T6_NET_DUMP_DIR");
    if (FILE* f = T6Open(fmt::format("{}/heap_{}.bin", d ? d : ".", g_net_frame), "wb")) {
      std::fwrite(g_t6_membase + 0xA7290000u, 1, 0xABA90000u - 0xA7290000u, f);
      std::fclose(f);
    }
  }
  {
    const double i0 = T6NowMs();
    g_net_where = "idle";
    ggpo_idle(g_ggpo, 0);
    g_net_where = "after idle";
    g_bp[1] = T6NowMs() - i0;
  }
  // GGPO's time sync ("you are N frames ahead"): a Sleep(16) per frame. Under the frame clock it only made single
  // frames late (16 ms or more on top of the frame, caught up by the next ones - no lasting shift, 2026-10-12): the
  // phase is held by the alignment before the fight and the balancer. Slept only with T6_NET_VSYNC=1 (no frame clock).
  if (g_frames_ahead > 0 && NetOwnPace()) {
    REXLOG_INFO("T6 net: frame {}: GGPO time sync asks for {} frames of waiting - not slept (frame clock)", g_net_frame, g_frames_ahead);
    g_frames_ahead = 0;
  }
  if (g_frames_ahead > 0) {
    Sleep(16);
    g_frames_ahead--;
    g_net_wait_ms += 16;
  }
  uint8_t local[kNetInput];
  {
    const double li0 = T6NowMs();
    LocalInput(local);
    LocalLatched(local);
    g_local_in_ms += T6NowMs() - li0;
  }
  // T6_NET_TEST_MASH=1 (tests, shader storage runs): random directions and attack buttons instead of the controller,
  // each held for a few frames - the fight shows hits, throws, effects. Never START (intros and win poses play out).
  if (static const bool mash = T6Env("T6_NET_TEST_MASH") != nullptr; mash) {
    static uint32_t rng = 0x9E3779B9u ^ GetCurrentProcessId();
    static uint8_t held[2] = {};
    static int left = 0;
    if (left-- <= 0) {
      rng = rng * 1664525u + 1013904223u;
      const uint32_t r = rng >> 8;
      held[0] = static_cast<uint8_t>((r & 3) == 0 ? 0 : ((0x10u << ((r >> 2) & 3)) | ((r & 0x40) ? (0x10u << ((r >> 4) & 3)) : 0)));  // X Y A B, sometimes two
      held[1] = static_cast<uint8_t>(((r >> 8) & 3) == 0 ? 0 : (1u << ((r >> 10) & 3)));  // d-pad
      left = 2 + static_cast<int>((r >> 12) % 12);
    }
    std::memset(local, 0, kNetInput);
    local[0] = held[0];
    local[1] = held[1];
  }
  // Rounds to win / round time of the launcher (T6_ROUNDS, T6_ROUND_TIME): the battle setup hook (sub_82245090) only
  // runs for the first match; a rematch rebuilt the match info with the game's defaults (2 rounds). They are written
  // again in every frame before the fight of a match (loading 14, intros 0-2, round call 3 of round 1), the same on
  // both instances.
  {
    const uint32_t gm = T6GameMode();
    if (gm == 14 || gm <= 3) {
      uint32_t mi = 0;
      T6StatusMatchInfo(&mi);
      if (mi >= 0x82726000u) {
        if (const char* r = T6Env("T6_ROUNDS"); r && T6ToInt(r) >= 1 && T6ToInt(r) <= 5) {
          if (g_t6_membase[mi + 282] != static_cast<uint8_t>(T6ToInt(r))) {
            g_t6_membase[mi + 282] = static_cast<uint8_t>(T6ToInt(r));
            t6_dirty_mark(mi + 282);
          }
          // +341 = most rounds of the match (sub_822CE348 ends the match when the round counter reaches it; game
          // default 3 for 2 to win): 2 * rounds - 1, else a first to 3 ended 2-1 after round 3
          if (g_t6_membase[mi + 341] != static_cast<uint8_t>(2 * T6ToInt(r) - 1)) {
            g_t6_membase[mi + 341] = static_cast<uint8_t>(2 * T6ToInt(r) - 1);
            t6_dirty_mark(mi + 341);
          }
        }
        if (const char* t = T6Env("T6_ROUND_TIME"); t && T6ToInt(t) >= 10) {
          const uint32_t v = _byteswap_ulong(static_cast<uint32_t>(T6ToInt(t)) * 60u);
          if (*reinterpret_cast<uint32_t*>(g_t6_membase + mi + 344) != v) {
            *reinterpret_cast<uint32_t*>(g_t6_membase + mi + 344) = v;
            t6_dirty_mark(mi + 344);
          }
        }
      }
    }
  }
  // After the match (win pose 7 / 8 / 9, then the menu REMATCH / SAVE REPLAY DATA / CHARACTER SELECT / RETURN TO MAIN
  // MENU in modes 11, 12, 25-28) the session keeps running and both instances stay in step. The players' own inputs are
  // dropped there (nobody leaves the match flow by hand). 'rematch' from the launcher (T6_NET_CMD, both sides): this
  // side presses A for 3 frames every 30 frames - skips the win pose, confirms the highlighted REMATCH - until the game
  // is out of those modes (loading of the next match). The presses travel through GGPO like any input.
  {
    const uint32_t gm = T6GameMode();
    const bool after_match = T6AfterMatchMode(gm);
    static bool rematch = false;
    static uint32_t rematch_from = 0;
    if (T6StatusTakeRematch()) {
      rematch = true;
      rematch_from = g_net_frame;
      g_result_cursor = 0;  // REMATCH is confirmed: the menu remembers it
      REXLOG_INFO("T6 net: rematch requested at frame {} (game mode {})", g_net_frame, gm);
    }
    if (after_match) {
      std::memset(local, 0, kNetInput);
      if (rematch && (g_net_frame - rematch_from) % 30 < 3) local[0] = 0x10;  // XINPUT_GAMEPAD_A 0x1000, high byte
    } else if (rematch && g_net_frame > rematch_from + 60) {
      rematch = false;
      REXLOG_INFO("T6 net: rematch: out of the match-end menu at frame {} (game mode {})", g_net_frame, gm);
    }
  }
  KeepDroppedInput(local);
  CrtRandSync();
  T6NamePlatesFrame(g_t6_membase);  // HUD name plates: the launcher's player names (t6_watch.cpp)
  const auto wait_queued = T6Rex().audio_queued;
  const int wait_q0 = wait_queued ? wait_queued() : 0;  // device queue before a wait for the peer's input
  const double w0 = T6NowMs();
  g_net_where = "input wait";
  for (;;) {
    const GGPOErrorCode err = ggpo_add_local_input(g_ggpo, g_net_local, local, kNetInput);
    if (GGPO_SUCCEEDED(err)) break;
    if (err != GGPO_ERRORCODE_PREDICTION_THRESHOLD && err != GGPO_ERRORCODE_NOT_SYNCHRONIZED) {
      REXLOG_ERROR("T6 net: ggpo_add_local_input failed: {}", static_cast<int>(err));
      break;
    }
    ggpo_idle(g_ggpo, 1);
    // (a press made while waiting here goes into this frame's input; the test inputs and the match-end menu keep theirs)
    if (LatchOn() && g_menu_frames == 0 && !T6Env("T6_NET_TEST_MASH")) {
      const NetConfig& c = net_config();
      const uint32_t gm = T6GameMode();
      const bool after_match = T6AfterMatchMode(gm);
      if (!after_match && !c.random_input && !c.step_input && !c.attack_input) {
        uint8_t now[kNetInput];
        LocalInput(now);
        MergePresses(local, now[0] & ~g_last_sent[0], now[1] & ~g_last_sent[1]);
        if (g_frame_dropped) {  // (GGPO drops this frame's input: the press waits in g_drop_acc)
          g_drop_acc[0] |= now[0] & ~g_last_sent[0];
          g_drop_acc[1] |= now[1] & ~g_last_sent[1];
        }
      }
    }
    // Result menu and no input from the peer for 100 ms: his game took its 'park' command first (the launchers' commands
    // are polled 4 times a second, not on the same frame) and closed the session. This side stood here until GGPO's
    // disconnect timeout - no frame, so no command poll either (e2e 2026-10-13: park 5 s after the peer's; the user's
    // log 2026-10-12: 1.85 s). The command is read here and the session closed at once.
    if (g_menu_frames > 0 && T6NowMs() - w0 > 100.0) {
      static double next_poll = 0;
      if (T6NowMs() >= next_poll) {
        next_poll = T6NowMs() + 50.0;
        T6StatusPoll();
        if (T6StatusTakePark()) g_park_wanted = true;
      }
      if (g_park_wanted) {
        REXLOG_INFO("T6 park: command taken in the input wait ({:.0f} ms without the peer's input, menu frames {})", T6NowMs() - w0, g_menu_frames);
        ParkSession(g_menu_frames < 60 ? 60 - g_menu_frames : 0);
        g_net_depth--;
        return true;
      }
    }
  }
  g_net_wait_ms += static_cast<uint64_t>(T6NowMs() - w0);
  g_last_sent[0] = local[0];  // (what went to GGPO, presses of the wait included)
  g_last_sent[1] = local[1];
  g_bp[2] = T6NowMs() - w0;
  if (g_bp[2] > 1.0) {  // (frame clock, see the pacing wait)
    g_clock_push_ms += g_bp[2];
    if (wait_queued) g_clock_audio_frames += std::max(0, wait_q0 - wait_queued());
  }
  g_net_where = "after input wait";
  {
    const double s0 = T6NowMs();
    SyncInputs();
    g_bp[3] = T6NowMs() - s0;
  }
  // Phase balance between the two sides (2026-10-11, rb16: 2.5 vs 13.4 rollbacks per 120 frames). Both games are paced
  // by their audio device, so the offset in time between the two sides' frames is set at the start and never
  // corrected (GGPO's whole-frame time sync sleeps did not move it: the audio wait takes the time back), and two
  // machines' audio clocks drift. The side whose remote inputs are more often not there yet when a frame starts runs
  // ahead: it moves its frames later by one audio frame (5.3 ms, rex_audio_delay) whenever its share of predicted
  // frames is clearly above the peer's (exchanged in GGPO's quality reports), at most once every 4 s.
  // T6_NET_PHASE=0 = off.
  {
    static const bool on = T6EnvDefaultOn("T6_NET_PHASE");
    static int predicted = 0, frames = 0, since = 100, lockstep_frames = 0;  // since = windows since the last shift
    predicted += g_ggpo_predicted;
    lockstep_frames += g_net_mode == 1;
    // (windows of 60 frames = 1 s, the interval of the quality reports: with 120 the first shift of a fight came
    // after 4 s at depth 2 - ph1h; cooldowns are in windows)
    if (++frames == 60) {
      // a window with lockstep frames does not count (no predictions there: ph1 shifted right after every intro);
      // -1 tells the peer the same
      g_ggpo_local_late = lockstep_frames ? -1 : predicted * 1000 / frames;
      since++;
      using DelayFn = void (*)(int);
      const auto delay = T6Rex().audio_delay;
      // deadband 250 per mille (ph1 with 120: the two sides took turns shifting); a shift moves ~1/3 of a frame.
      // A peer that predicts (nearly) nothing while this side predicts every frame is most of a frame behind (tm1:
      // 1000 against 0-130 per mille for the first 22 s of the fight, four single shifts 6 s apart; this side had
      // 10.5 rollbacks per 120 frames at depth 1.35, the peer 5.7): two audio frames at once, next check after 2
      // windows.
      const int diff = g_ggpo_local_late - g_ggpo_remote_late;
      // A big shift may follow any shift after 2 windows (the peer's value is of the window after the shift by
      // then), a half step only after 6: ph2h, round 2 - a half step at 1000 / 616, the next window was 1000 / 16
      // and the side stayed 6 s at depth 2 behind the half step's cooldown.
      const bool ok = on && delay && g_ggpo_local_late >= 0 && g_ggpo_remote_late >= 0;
      // Near the balance the step is HALF an audio frame (2.7 ms): at 50 ms ping the band where both sides predict
      // with depth 1 is narrower than 5.3 ms (pf1: a single 5.3 ms shift of one side went from 1000 / 700 per mille
      // straight to 16 / 1000, the peer at depth 2 again). Half a frame has no audio frame to add: the frame clock's
      // queue depth target goes down by 0.5 instead, and gets its frame back (one frame queued twice) below 3.25.
      // (half steps from 450 per mille: ph3h - with 250 the two sides took turns every ~3 s between 1000 / 650 and
      // 650 / 1000, both of which are depth 1)
      const int units = ok && diff > 800 && since >= 2 ? 2 : ok && diff > 450 && since >= 6 ? 1 : 0;
      REXLOG_INFO("T6 net: frame {}: predicted remote input in {} per mille of the live frames, peer {}{}", g_net_frame, g_ggpo_local_late,
                  g_ggpo_remote_late, units == 2 ? " -> this side moves its frames 10.7 ms later" : units ? " -> this side moves its frames 2.7 ms later" : "");
      if (units == 2 && NetOwnPace()) {
        // the frames 10.7 ms later, spread by the frame clock, which also gives the device its two extra audio frames
        g_phase_shift_ms += 2 * 256000.0 / 48000.0;
        g_phase_audio_ms += 2 * 256000.0 / 48000.0;
        since = 0;
      } else if (units == 2) {
        delay(2);  // (T6_NET_VSYNC=1: paced by the audio device)
        since = 0;
      } else if (units == 1 && NetOwnPace()) {
        g_phase_shift_ms += 0.5 * 256000.0 / 48000.0;
        g_depth_target -= 0.5;
        if (g_depth_target < 3.25) {
          delay(1);
          g_depth_target += 1.0;
        }
        since = 0;
      } else if (units == 1) {
        delay(1);  // (T6_NET_VSYNC=1: paced by the audio device, whole audio frames only)
        since = 0;
      }
      predicted = frames = lockstep_frames = 0;
    }
  }
  // T6_NET_TEST_STALL=<frame>,<ms> (tests): one stall of this side at that session frame, like a hitch of a slow PC
  // in the intro (the frame clock starts again after a stall over 50 ms, so this side stays that much behind)
  if (static const char* ts = T6Env("T6_NET_TEST_STALL"); ts && *ts) {
    static const uint32_t at = std::strtoul(ts, nullptr, 0);
    static const DWORD ms = std::strchr(ts, ',') ? std::strtoul(std::strchr(ts, ',') + 1, nullptr, 0) : 60;
    // (a third field = only that player: "1500,70,2")
    static const int who = std::strchr(ts, ',') && std::strchr(std::strchr(ts, ',') + 1, ',') ? T6ToInt(std::strchr(std::strchr(ts, ',') + 1, ',') + 1) : 0;
    if (g_net_frame == at && (!who || who == net_config().player)) Sleep(ms);
  }
  // Coarse alignment BEFORE the fight (user 2026-10-12: "tohle se musi srovnat nez zazni fight"). The lockstep phase
  // (intros, round call, between rounds) runs with an input delay of 4 frames, so one side can be up to ~3 frames
  // ahead of the other there without ever waiting; hitches of the peer in the intro put it there. At the round start
  // the delay goes back to 1 and the prediction window is 3 frames: the side that is ahead hit the window in nearly
  // every frame (user's log, first round: 101 of 120 frames late, 16 over 25 ms) until the balancer above had moved
  // it three times by 10.7 ms, 2 s apart. Here, in lockstep, once a second: GGPO's two frame advantages (ours and the
  // one the peer reports; their difference does not depend on the input delay) give how many whole frames this side
  // is ahead; two readings in a row that agree move this side's frames later by that much at once (2 frames at
  // most per step, the smaller of the two readings). The balancer does the fine part (< 1 frame) in the fight.
  // T6_NET_ALIGN=0 = off.
  {
    static const bool on = T6EnvDefaultOn("T6_NET_ALIGN");
    static int prev = 0, wait = 0;
    static uint32_t last = 0;
    if (g_net_frame < last) prev = wait = 0;  // new session
    last = g_net_frame;
    if (wait > 0) wait--;
    if (on && g_net_mode == 1 && g_net_frame >= 120 && g_net_frame % 60 == 0) {  // (120: with skipped intros the fight starts near frame 400)
      GGPONetworkStats st{};
      ggpo_get_network_stats(g_ggpo, g_net_remote, &st);
      int remote = st.timesync.remote_frames_behind;
      if (remote > 127) remote -= 256;  // (sent as uint8)
      const int ahead = (remote - st.timesync.local_frames_behind) / 2;  // whole frames this side is ahead
      const int frames = wait ? 0 : std::min({ahead, prev, 2});
      if (frames >= 1) {
        const auto delay = T6Rex().audio_delay;
        const int units = frames * 3;  // audio frames of 5.33 ms: 16 ms per game frame
        if (NetOwnPace()) {
          g_phase_shift_ms += units * 256000.0 / 48000.0;  // (spread by the frame clock, with the audio frames)
          g_phase_audio_ms += units * 256000.0 / 48000.0;
        } else if (delay) {
          delay(units);
        }
        wait = 90;  // the peer's next report has to show the result first
        prev = 0;
      } else {
        prev = ahead;
      }
      if (ahead != 0) {
        REXLOG_INFO("T6 net: frame {}: before the fight this side is {} frames ahead (advantage local {}, peer {}){}", g_net_frame, ahead,
                    st.timesync.local_frames_behind, remote, frames >= 1 ? fmt::format(" -> its frames move {} ms later", frames * 16) : std::string());
      }
    }
  }
  // T6_NET_DUMP=1: FNV hash of every committed 4 KB page of the game memory (XEX data/bss, game heaps, game pages of
  // 0x4xxxxxxx) at session frames 0, 1, 60, 300 -> <log dir>/pages_<frame>.txt (compare both instances, PS3 region_diff)
  if (T6Env("T6_NET_DUMP") && (g_net_frame == 0 || g_net_frame == 1 || g_net_frame == 60 || g_net_frame == 300)) {
    const char* dir = T6Env("T6_NET_DUMP_DIR");
    if (FILE* f = T6Open(fmt::format("{}/pages_{}.txt", dir ? dir : ".", g_net_frame), "w")) {
      const auto dump = [&](uint32_t lo, uint32_t hi) {
        ForEachCommitted(lo, hi, [&](uint32_t a, uint32_t n) {
          for (uint32_t p = a; p + 0x1000 <= a + n; p += 0x1000) {
            std::fprintf(f, "%08X %016llX\n", p, static_cast<unsigned long long>(Fnv(g_t6_membase + p, 0x1000)));
          }
        });
      };
      dump(0x82726000u, 0x82DB0000u);
      dump(0xA7290000u, 0xABA90000u);
      for (uint32_t q = 0x40000; q < 0x50000; q++) {
        if (g_pre_pages[q - 0x40000] || (t6_pagemap[q] & 0x01)) dump(q << 12, (q + 1) << 12);
      }
      std::fclose(f);
    }
  }
  // T6_NET_PAGEHASH=<from>,<to>: before each live frame in that range, FNV of every 4 KB page of the XEX data/bss and
  // of the game heaps -> <dump dir>/pagehash.bin (u32 frame, u32 count, count x (u32 page address, u64 hash)).
  // tools/pagehash_diff.py compares two instances: first frame and pages where the state differs.
  if (static const char* ph = T6Env("T6_NET_PAGEHASH"); ph && *ph) {
    static const uint32_t from = std::strtoul(ph, nullptr, 0);
    static const uint32_t to = std::strchr(ph, ',') ? std::strtoul(std::strchr(ph, ',') + 1, nullptr, 0) : from + 100;
    static FILE* f = [] { const char* d = T6Env("T6_NET_DUMP_DIR"); return T6Open(fmt::format("{}/pagehash.bin", d ? d : "."), "wb"); }();
    if (f && g_net_frame >= from && g_net_frame <= to) {
      std::vector<std::pair<uint32_t, uint64_t>> rows;
      for (const auto& [lo, hi] : kT6GameRanges) {
        for (uint32_t a = lo; a < hi; a += 0x1000) {
          if (lo >= 0xA0000000u && !t6_pagemap[a >> 12]) continue;  // heaps: pages written since the session start
          rows.emplace_back(a, Fnv(g_t6_membase + a, 0x1000));
        }
      }
      const uint32_t hdr[2] = {g_net_frame, static_cast<uint32_t>(rows.size())};
      std::fwrite(hdr, 4, 2, f);
      for (const auto& [a, h] : rows) {
        std::fwrite(&a, 4, 1, f);
        std::fwrite(&h, 8, 1, f);
      }
      std::fflush(f);
    }
  }
  // T6_NET_RAWDUMP=<from>,<to>: the same pages as raw bytes -> <dump dir>/raw_<frame>.bin (u32 page address + 4096
  // bytes each); tools/raw_diff.py lists the differing words of two instances.
  if (static const char* rd = T6Env("T6_NET_RAWDUMP"); rd && *rd) {
    static const uint32_t from = std::strtoul(rd, nullptr, 0);
    static const uint32_t to = std::strchr(rd, ',') ? std::strtoul(std::strchr(rd, ',') + 1, nullptr, 0) : from;
    if (g_net_frame >= from && g_net_frame <= to) {
      const char* d = T6Env("T6_NET_DUMP_DIR");
      if (FILE* f = T6Open(fmt::format("{}/raw_{}.bin", d ? d : ".", g_net_frame), "wb")) {
        for (const auto& [lo, hi] : kT6GameRanges) {
          for (uint32_t a = lo; a < hi; a += 0x1000) {
            if (lo >= 0xA0000000u && !t6_pagemap[a >> 12]) continue;
            std::fwrite(&a, 4, 1, f);
            std::fwrite(g_t6_membase + a, 1, 0x1000, f);
          }
        }
        std::fclose(f);
      }
    }
  }
  if (!audio_done) {
    const double u0 = T6NowMs();
    AudioStepOrJoin(g_net_frame);
    g_bp[4] = T6NowMs() - u0;
    // Deterministic loading: what the previous frames requested is loaded before this frame runs
    const double l0 = T6NowMs();
    if (!WaitLoaderIdle(5000)) REXLOG_WARN("T6 net: frame {}: file loader still busy after 5 s", g_net_frame);
    g_loader_wait_ms += T6NowMs() - l0;
    g_bp[5] = T6NowMs() - l0;
  }
  g_net_pending = true;
#ifdef T6_PGO_GEN
  // Profile-guided build, step 1 (build_pgo.cmd): the counters of the instrumented exe are written at session frame
  // 4000 (the test kills the processes, so nothing is written at exit)
  if (g_net_frame == 4000) {
    REXLOG_INFO("T6 net: profile written: {}", __llvm_profile_write_file());
  }
#endif
  if (g_net_frame < 10 || g_net_frame % 30 == 0) T6RandLog(g_net_frame);
  if (++g_net_frames % 120 == 0) {
    GGPONetworkStats st{};
    ggpo_get_network_stats(g_ggpo, g_net_remote, &st);
    const double n = static_cast<double>(g_net_frames + g_net_resim_frames);
    REXLOG_INFO("T6 net: frame {}: {} live, {} re-simulated in {} rollbacks, waited {} ms, ping {} ms, remote behind {}, "
                "save {:.2f} ms/frame (net worker {:.2f}, jobs {:.2f}, calibration {:.2f}, copy {:.2f}, {} KB), load {:.2f} ms/rollback, "
                "{:.1f} live frames/s, loader wait {:.0f} ms",
                g_net_frame, g_net_frames, g_net_resim_frames, g_net_loads, g_net_wait_ms, st.network.ping,
                st.timesync.remote_frames_behind, g_save_ms / n, g_save_parts[0] / n, g_save_parts[1] / n, g_save_parts[2] / n,
                g_save_parts[3] / n, g_save_bytes / static_cast<uint64_t>(n) >> 10, g_net_loads ? g_load_ms / g_net_loads : 0.0,
                120000.0 / (T6NowMs() - g_stats_t0), g_loader_wait_ms);
    g_stats_t0 = T6NowMs();
    {
      // images handed to the screen in these 120 frames (120 = one per frame; an overlay that shows twice the game's
      // frame rate = frame generation of the driver, which makes every late frame worse)
      const auto swaps = T6Rex().swap_count;
      static uint32_t last_swaps = 0;
      const uint32_t now_swaps = swaps ? swaps() : 0;
      if (swaps) REXLOG_INFO("T6 net: frame {}: {} images presented in the last 120 frames", g_net_frame, now_swaps - last_swaps);
      last_swaps = now_swaps;
    }
    REXLOG_INFO("T6 net: frame {}: frame times of the last 120: period avg {:.2f} max {:.1f} ms, over 17.5 ms {}, over 25 ms {}; "
                "boundary avg {:.2f} max {:.1f} ms, over 8 ms {}; the longest: advance {:.1f}, idle {:.1f}, input wait {:.1f}, "
                "sync inputs {:.1f}, audio {:.1f}, loader {:.1f} ms, {:.0f} re-simulated, load {:.1f} ms", g_net_frame, g_ft_period_sum / 120, g_ft_period_max, g_ft_over17,
                g_ft_over25, g_ft_bound_sum / 120, g_ft_bound_max, g_ft_bound_over8, g_bp_max[0], g_bp_max[1], g_bp_max[2], g_bp_max[3],
                g_bp_max[4], g_bp_max[5], g_bp_max[6], g_bp_max[7]);
    {
      static double last_ms = 0, last_load = 0;
      static uint64_t last_n = 0, last_loads = 0;
      const uint64_t dn = g_net_resim_frames - last_n, dl = g_net_loads - last_loads;
      static double last_save = 0, last_parts[4] = {};
      const double saves = 120.0 + static_cast<double>(dn);  // one save per live and per re-simulated frame
      REXLOG_INFO("T6 net: frame {}: last 120 frames: {} rollbacks, {} re-simulated frames, game step of a re-simulated frame {:.2f} ms, "
                  "load {:.2f} ms, save {:.2f} ms (draw worker wait {:.2f}, jobs {:.2f}, calibration {:.2f}, copy {:.2f}); whole rollback "
                  "avg {:.2f} max {:.1f} ms; state note {:.2f} ms/save; pacing wait avg {:.2f} ms, {} frames late, clock moved {:.0f} ms for input waits", g_net_frame, dl, dn,
                  dn ? (g_resim_ms - last_ms) / dn : 0.0, dl ? (g_load_ms - last_load) / dl : 0.0, (g_save_ms - last_save) / saves,
                  (g_save_parts[0] - last_parts[0]) / saves, (g_save_parts[1] - last_parts[1]) / saves,
                  (g_save_parts[2] - last_parts[2]) / saves, (g_save_parts[3] - last_parts[3]) / saves,
                  dl ? g_rb_ms / dl : 0.0, g_rb_max, g_note_ms / saves, g_ft_slack_sum / 120, g_ft_late, g_clock_pushed_sum);
      g_clock_pushed_sum = 0;
      REXLOG_INFO("T6 net: frame {}: rollback parts of the window (ms): load {:.2f}, between {:.2f}, sync inputs {:.2f}, game step {:.2f}, "
                  "loader wait {:.2f}, advance+save {:.2f}", g_net_frame, g_load_ms - g_rbp_load0, g_rbp[4], g_rbp[0], g_rbp[1], g_rbp[2], g_rbp[3]);
      g_rbp_load0 = g_load_ms;
      REXLOG_INFO("T6 net: frame {}: saves inside rollbacks (ms, window): draw worker lock {:.2f}, job workers {:.2f}, global lock {:.2f}, "
                  "copy {:.2f}; undo buffer growth {} times {:.2f} ms; pages per save: rollback {:.0f}, live {:.0f}", g_net_frame, g_rbsave[0],
                  g_rbsave[1], g_rbsave[2], g_rbsave[3], g_inc_resizes, g_inc_resize_ms,
                  g_inc_rb_saves ? static_cast<double>(g_inc_rb_pages) / g_inc_rb_saves : 0.0,
                  g_inc_live_saves ? static_cast<double>(g_inc_live_pages) / g_inc_live_saves : 0.0);
      g_inc_resizes = 0; g_inc_resize_ms = 0; g_inc_rb_pages = g_inc_rb_saves = g_inc_live_pages = g_inc_live_saves = 0;
      for (double& v : g_rbsave) v = 0;
      for (double& v : g_rbp) v = 0;
      REXLOG_INFO("T6 net: frame {}: audio queue depth avg {:.2f} frames; audio step (ms/frame): callbacks {:.2f}, XAudio thread {:.2f}, "
                  "CnuSound tick {:.2f}", g_net_frame, g_depth_sum / 120, g_audio_parts[0] / 120, g_audio_parts[1] / 120, g_audio_parts[2] / 120);
      g_audio_parts[0] = g_audio_parts[1] = g_audio_parts[2] = 0;
      {
        const auto xf = T6Rex().xma_stats;
        static uint64_t ns0 = 0, n0 = 0;
        uint64_t ns = 0, n = 0;
        if (xf) {
          xf(&ns, &n);
          REXLOG_INFO("T6 net: frame {}: XMA decodes of the last 120 frames: {:.1f} per frame, {:.2f} ms per frame", g_net_frame,
                      static_cast<double>(n - n0) / 120, static_cast<double>(ns - ns0) / 1e6 / 120);
          ns0 = ns;
          n0 = n;
          if (static uint32_t windows = 0; ++windows % 5 == 0) T6VoiceStatsLog(g_net_frame);
        }
      }
      REXLOG_INFO("T6 net: frame {}: load draw idle wait of the window (ms, overlapping): ring {:.2f}, fences {:.2f}, swap {:.2f}; "
                  "pages held back from loads so far {}", g_net_frame, g_drawidle_ms[0], g_drawidle_ms[1], g_drawidle_ms[2], g_load_held);
      g_drawidle_ms[0] = g_drawidle_ms[1] = g_drawidle_ms[2] = 0;
      g_depth_sum = 0;
      {
        static uint64_t last_skipped = 0;
        REXLOG_INFO("T6 net: frame {}: last 120 frames: {} live frames not drawn (late), {} saves of re-simulated frames left out",
                    g_net_frame, g_ft_skipped, g_resim_saves_skipped - last_skipped);
        last_skipped = g_resim_saves_skipped;
        g_ft_skipped = 0;
      }
      g_rb_ms = g_rb_max = g_note_ms = g_ft_slack_sum = 0;
      g_ft_late = 0;
      last_ms = g_resim_ms; last_load = g_load_ms; last_n = g_net_resim_frames; last_loads = g_net_loads;
      last_save = g_save_ms;
      for (int k = 0; k < 4; k++) last_parts[k] = g_save_parts[k];
    }
    // Where an average frame of the window goes (PS3 RB_SLOW_FRAMES): game frame = end of the boundary to the next
    // boundary (task tree step with the draw hand-over), then the parts of the boundary
    REXLOG_INFO("T6 net: frame {}: average frame of the last 120: game frame {:.2f} ms, audio {:.2f}, loader {:.2f}, advance (save + rollbacks) "
                "{:.2f}, idle {:.2f}, input wait {:.2f}, sync inputs {:.2f} ms", g_net_frame, g_ft_game_sum / 120, g_bp_sum[4] / 120,
                g_bp_sum[5] / 120, g_bp_sum[0] / 120, g_bp_sum[1] / 120, g_bp_sum[2] / 120, g_bp_sum[3] / 120);
    REXLOG_INFO("T6 net: frame {}: draw of the last 120 (main thread, ms/frame): draw subtree walk {:.2f}, draw pass {:.2f}, of them "
                "D3D fence wait {:.2f}", g_net_frame, g_draw_ms[0] / 120, g_draw_ms[1] / 120, g_draw_ms[2] / 120);
    REXLOG_INFO("T6 net: frame {}: sound boundary of the last 120 (ms/frame): library calls {:.2f} (with the store capture), record "
                "overhead {:.2f}, replays {:.2f}; {} recorded calls and {} captured stores per frame", g_net_frame, g_snd_ms[0] / 120,
                g_snd_ms[1] / 120, g_snd_ms[2] / 120, g_snd_caps[1] / 120, g_snd_caps[0] / 120);
    g_snd_ms[0] = g_snd_ms[1] = g_snd_ms[2] = 0;
    g_snd_caps[0] = g_snd_caps[1] = 0;
    g_draw_ms[0] = g_draw_ms[1] = g_draw_ms[2] = 0;
    {
      const auto sf = T6Rex().sem_stats;
      static uint64_t c0 = 0, t0 = 0;
      uint64_t c = 0, t = 0;
      if (sf) {
        sf(&c, &t);
        LARGE_INTEGER fq;
        QueryPerformanceFrequency(&fq);
        const double ms = static_cast<double>(t - t0) * 1000.0 / static_cast<double>(fq.QuadPart);
        REXLOG_INFO("T6 net: frame {}: semaphore releases of the last 120 frames (all threads): {:.0f} per frame, {:.2f} ms per frame, "
                    "{:.1f} us each", g_net_frame, static_cast<double>(c - c0) / 120, ms / 120, c > c0 ? ms * 1000.0 / static_cast<double>(c - c0) : 0.0);
        c0 = c;
        t0 = t;
      }
    }
    g_ft_game_sum = 0;
    for (double& v : g_bp_sum) v = 0;
    REXLOG_INFO("T6 net: frame {}: pad step {:.2f} ms/frame, host pad reads {} in {:.1f} ms, local input {:.2f} ms/frame; exceptions "
                "caught in destructors so far {}", g_net_frame, t6_pad_step_ms / 120, g_pad_host_n, g_pad_host_ms, g_local_in_ms / 120,
                T6SwallowedExceptions().load());
    t6_pad_step_ms = g_pad_host_ms = g_local_in_ms = 0;
    g_pad_host_n = 0;
    g_ft_period_sum = g_ft_period_max = g_ft_bound_sum = g_ft_bound_max = 0;
    g_ft_over17 = g_ft_over25 = g_ft_bound_over8 = 0;
  }
  g_net_depth--;
  return true;
}

// File request insert (NU::File::RequestManager, sub_82525280 this=r3, request r4; pending list sentinel this+10348,
// head *(this+10364)). Overrides the weak symbol: remembers the queue objects (deterministic loading in a session).
uint32_t g_file_queues[4] = {};
uint64_t g_file_requests = 0;
