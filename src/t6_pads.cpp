#include "t6_advanced_practice.h"
// Tekken 6 netplay - controllers: session inputs for XInputGetState, host pad calls (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// a pad state the game gets from a script (XINPUT_STATE: packet number = the pad step, nothing pressed)
void PadReset(uint8_t* st) {
  std::memset(st, 0, 16);
  *reinterpret_cast<uint32_t*>(st) = _byteswap_ulong(state().step);
}
extern "C" void sub_826B58E8(PPCContext& ctx, uint8_t* base) {
  // T6_AUTO_CONFIRM=1 (menu driver like the PS3 RB_PAD_CMD): after the replay ended, users 0 / 1 press A for 3 frames
  // every 40 steps (offset 20 for user 1) -> both confirm the default character / stage (memory diff for StartMatch)
  if (static const bool autoc = T6Env("T6_AUTO_CONFIRM") != nullptr; autoc && state().play_done && !g_net_running && ctx.r3.u32 < 2) {
    const uint32_t ph = (state().step + (ctx.r3.u32 ? 20u : 0u)) % 40u;
    uint8_t* st = base + ctx.r4.u32;
    PadReset(st);
    // T6_VS_SEQ1 / T6_VS_SEQ2=<hex wButtons>:<frames>,... (tests, finding the game's "can act" for the frame data, user
    // 2026-10-14: "tohle musis nejak spocitat"): in the Versus fight, users 0 / 1 play these sequences, started every
    // T6_VS_PERIOD (200) steps from T6_VS_FROM (600) steps after the battle exists; no A presses in the fight. With
    // T6_VS_MASH1 / T6_VS_MASH2=<offset> the user taps 1 (every other step) from that offset of the period to its end:
    // a buffered button is taken at the first frame the player can act = the truth to compare the fields with.
    if (static const bool vs_seq = T6Env("T6_VS_SEQ1") || T6Env("T6_VS_SEQ2"); vs_seq) {
      static bool battle = false;
      static uint32_t battle_at = 0;
      if (!battle && ctx.r3.u32 == 0 && state().step % 10 == 0 && TaskTreeHas(0x8200C5E8u, 0x8200C5E8u)) {
        battle = true;
        battle_at = state().step;
        REXLOG_INFO("T6 vs seq: battle at step {}", battle_at);
      }
      if (battle) {
        static const std::vector<std::pair<uint16_t, uint32_t>> seqs[2] = {T6ParseSeq(T6Env("T6_VS_SEQ1")), T6ParseSeq(T6Env("T6_VS_SEQ2"))};
        static const char* mash_env[2] = {T6Env("T6_VS_MASH1"), T6Env("T6_VS_MASH2")};
        static const uint32_t period = T6EnvU32("T6_VS_PERIOD", 200, 10);
        static const uint32_t from = T6EnvU32("T6_VS_FROM", 600, 10);
        const uint32_t u = ctx.r3.u32;
        uint16_t b = 0;
        if (state().step >= battle_at + from) {
          const uint32_t at = (state().step - battle_at - from) % (period ? period : 1);  // (T6_VS_PERIOD=0)
          b = T6SeqAt(seqs[u], at);
          // (T6_VS_MASHn=<from>[:<frames>]: only that window, so the taps do not go on hitting the other player)
          if (mash_env[u]) {
            char* e = nullptr;
            const uint32_t m0 = std::strtoul(mash_env[u], &e, 10), mn = *e == ':' ? std::strtoul(e + 1, nullptr, 10) : period;
            // (T6_VS_MASHBTN1 / 2 = the tapped button, default 4000 = 1; 0001 = up: a jump also shows "can act" and no
            //  string goes on with it - Lili's 3 took the tapped 1 as 3,1)
            static const uint16_t mb[2] = {static_cast<uint16_t>(T6EnvU32("T6_VS_MASHBTN1", 0x4000, 16)),
                                           static_cast<uint16_t>(T6EnvU32("T6_VS_MASHBTN2", 0x4000, 16))};
            if (at >= m0 && at < m0 + mn && at % 2 == 0) b |= mb[u];  // (with what the sequence holds)
          }
          if (at == 0 && u == 0) REXLOG_INFO("T6 vs seq: period starts at step {}", state().step);
          // T6_VS_RANDOM=<seed> (tests, the stance audit): random play instead - a direction held 4-14 steps (none,
          // forward, back, down, up, diagonals) and button taps of 1-2 steps (one button, sometimes a pair)
          if (static const char* rnd_env = T6Env("T6_VS_RANDOM"); rnd_env) {
            static uint64_t rs[2] = {std::strtoull(rnd_env, nullptr, 10) * 2654435761ull + 1, std::strtoull(rnd_env, nullptr, 10) * 40503ull + 7};
            static uint32_t dir_until[2] = {}, tap_until[2] = {};
            static uint16_t dir[2] = {}, tap[2] = {};
            const auto next = [&](uint32_t n) { rs[u] ^= rs[u] << 13; rs[u] ^= rs[u] >> 7; rs[u] ^= rs[u] << 17; return static_cast<uint32_t>(rs[u] % n); };
            if (state().step >= dir_until[u]) {
              static const uint16_t dirs[] = {0, 0, 0, 0x0008, 0x0004, 0x0004, 0x0002, 0x0001, 0x000A, 0x0006, 0x0009, 0x0005, 0x0004, 0x0008};
              dir[u] = dirs[next(sizeof(dirs) / sizeof(dirs[0]))];
              dir_until[u] = state().step + 4 + next(11);
            }
            if (state().step >= tap_until[u] + 2 && next(4) == 0) {
              static const uint16_t btns[] = {0x4000, 0x8000, 0x1000, 0x2000, 0x4000, 0x8000, 0x1000, 0x2000, 0xC000, 0x3000, 0x6000};
              tap[u] = btns[next(sizeof(btns) / sizeof(btns[0]))];
              tap_until[u] = state().step + 1 + next(2);
            }
            b = dir[u] | (state().step < tap_until[u] ? tap[u] : 0);
          }
        }
        g_audit_in[u][2] = g_audit_in[u][1];
        g_audit_in[u][1] = g_audit_in[u][0];
        g_audit_in[u][0] = b;
        st[4] = static_cast<uint8_t>(b >> 8);
        st[5] = static_cast<uint8_t>(b);
        ctx.r3.u64 = 0;
        return;
      }
    }
    // T6_AUTO_CONFIRM=2: probe - user 0 presses RIGHT (0x0008) every 40 steps instead of A, cursor values logged
    static const bool probe = T6Env("T6_AUTO_CONFIRM")[0] == '2';
    if (probe) {
      if (ctx.r3.u32 == 0 && ph < 3) st[5] = 0x08;
      if (ctx.r3.u32 == 0 && ph == 20) {
        REXLOG_INFO("T6 probe step {}: A78591D8={} A785BED8={} A785BB94={}", state().step, T6Rd32(0xA78591D8u), T6Rd32(0xA785BED8u), T6Rd32(0xA785BB94u));
      }
    }
    if (!probe && ph < 3) st[4] = 0x10;  // wButtons A = 0x1000, big endian high byte
    // user 1 joins the Versus select with ONE START press (0x0010) 20 steps after the replay ended (more START presses
    // left the select screen, bd5)
    static uint32_t stop_step = 0;
    if (!stop_step) stop_step = state().step;
    if (ctx.r3.u32 == 1 && state().step >= stop_step + 20 && state().step < stop_step + 23) {
      st[4] = 0;
      st[5] = 0x10;
    }
    if (ctx.r3.u32 == 1 && state().step < stop_step + 60) st[4] = st[4] & ~0x10;  // no A for user 1 before it joined
    ctx.r3.u64 = 0;
    return;
  }
  // T6_MENU_SEQ=<hex wButtons>:<frames>,... (research 2026-10-15, the way to the game's Customize menu): after the menu
  // replay (T6_PLAY_UNTIL) user 0 plays that sequence once, then the player's controller + keyboard; user 1 no pad
  if (ctx.r3.u32 == 0 && state().play_done) T6KeepMoney(base);
  if (static const char* mseq = T6Env("T6_MENU_SEQ"); mseq && *mseq && state().play_done && ctx.r3.u32 < 2) {
    static std::vector<std::pair<uint16_t, uint32_t>> steps;
    static uint32_t from = 0, total = 0;
    if (!from) {
      from = state().step;
      steps = T6ParseSeq(mseq, &total);
      REXLOG_INFO("T6 menu seq: {} steps from step {}", total, from);
    }
    if (ctx.r3.u32 == 1) {
      ctx.r3.u64 = 1167;  // no pad
      return;
    }
    uint8_t* st = base + ctx.r4.u32;
    if (static const bool custom = T6Env("T6_CUSTOM_OUT") != nullptr; custom) {
      static bool task = false;
      if ((state().step - from) % 30 == 0) task = TaskTreeHas(0x8208B714u, 0x8208B714u);
      T6CustomizeTick(base, task);
    }
    if (static const char* ws = T6Env("T6_WATCH_SAVE"); ws && state().step - from == std::strtoul(ws, nullptr, 10)) T6WatchSave(base);
    // (research) the screens: the task tree's types every 30 steps, logged when they change
    if (static const bool tt = T6Env("T6_MENU_TASKS") != nullptr; tt && (state().step - from) % 30 == 0) {
      static std::vector<uint32_t> last;
      std::vector<uint32_t> now = TaskTreeVtables();
      std::sort(now.begin(), now.end());
      now.erase(std::unique(now.begin(), now.end()), now.end());
      if (now != last) {
        std::string add, gone;
        for (uint32_t v : now) if (!std::binary_search(last.begin(), last.end(), v)) add += fmt::format(" {:08X}", v);
        for (uint32_t v : last) if (!std::binary_search(now.begin(), now.end(), v)) gone += fmt::format(" {:08X}", v);
        REXLOG_INFO("T6 menu tasks at seq step {}: +[{}] -[{}]", state().step - from, add, gone);
        last = std::move(now);
      }
    }
    if (const uint32_t at = state().step - from; at < total) {
      const uint16_t bb = T6SeqAt(steps, at);
      PadReset(st);
      st[4] = static_cast<uint8_t>(bb >> 8);
      st[5] = static_cast<uint8_t>(bb);
      ctx.r3.u64 = 0;
      return;
    }
    __imp__sub_826B58E8(ctx, base);
    RemapPad(st + 4);
    if (const uint16_t kb = KeyboardButtons()) {
      if (ctx.r3.u32 != 0) {
        PadReset(st);
        ctx.r3.u64 = 0;
      }
      st[4] |= static_cast<uint8_t>(kb >> 8);
      st[5] |= static_cast<uint8_t>(kb);
    }
    return;
  }
  if (T6AdvancedGuestAvailable() && T6Env("T6_PRACTICE") && state().play_done && ctx.r3.u32 < 2) {
    const uint32_t user=ctx.r3.u32;
    uint8_t* st=base+ctx.r4.u32;
    if (!T6AdvancedManualSelection() && !g_practice_battle) {
      static const uint32_t began=state().step;
      const uint32_t d=state().step-began;
      PadReset(st);
      if (user==0 && d<130) {
        for (uint32_t k=0;k<6;k++) if (d>=10+k*15 && d<13+k*15) st[5]=0x02;
      } else if (user==0 && d<600 && d%40<3) st[4]=0x10;
      ctx.r3.u64=user ? 1167 : 0;
      return;
    }
    __imp__sub_826B58E8(ctx,base);
    if (user==0) {
      RemapPad(st+4);
      if (const uint16_t kb=KeyboardButtons()) {
        if (ctx.r3.u32) { PadReset(st); ctx.r3.u64=0; }
        st[4]|=static_cast<uint8_t>(kb>>8); st[5]|=static_cast<uint8_t>(kb);
      }
    }
    return;
  }
  if (!T6AdvancedGuestAvailable() && g_park && ctx.r3.u32 < 2) {
    // result menu: REMATCH / SAVE REPLAY DATA / CHARACTER SELECT / RETURN TO MAIN MENU -> down, down, A (user 0); then
    // both users confirm (A every 40 steps) until the next battle exists
    const uint32_t user = ctx.r3.u32;
    const int32_t d0 = static_cast<int32_t>(state().step - g_park_step0);  // (negative while the menu still has to stand)
    uint8_t* st = base + ctx.r4.u32;
    PadReset(st);
    for (int k = 0; k < g_park_downs; k++) {
      if (user == 0 && d0 >= 40 - 15 * k && d0 < 43 - 15 * k) st[5] = 0x02;  // XINPUT_GAMEPAD_DPAD_DOWN
    }
    // A = CHARACTER SELECT and its second press, until the battle task is gone
    if (user == 0 && !g_park_t1 && d0 >= 55 && (d0 - 55) % 10 < 3) st[4] = 0x10;
    // character select and the first press in the stage select: the old pulses, counted from T1 (old step = T1 + 240:
    // user 0 at old 270, 310, ..., 470 = T1 + 30 + 40 n, user 1 at old 250, 290, ..., 490 = T1 + 10 + 40 n)
    if (const int32_t t = g_park_t1 ? static_cast<int32_t>(state().step - g_park_t1) : -1; t >= 10 && t < 255 && (t + (user ? 30 : 10)) % 40 < 3) st[4] = 0x10;
    // 'start' received: user 0 confirms the stage
    if (g_park_started && user == 0 && state().step >= g_park_confirm + 2 && state().step < g_park_confirm + 5) st[4] = 0x10;
    ctx.r3.u64 = 0;
    return;
  }
  if (g_net_running && g_net_valid && ctx.r3.u32 < 2) {
    if (t6_thread_bit != 0) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        REXLOG_WARN("T6 net: pad read on a thread other than the main thread");
      }
    }
    uint8_t* st = base + ctx.r4.u32;
    *reinterpret_cast<uint32_t*>(st) = _byteswap_ulong(g_net_frame + 1);  // dwPacketNumber
    std::memcpy(st + 4, g_net_in[ctx.r3.u32], 12);
    // No START (0x0010, pause menu) / BACK (0x0020) in an online match - user 2026-10-12. START stays in the round
    // intros (game modes 0-2) and the KO replay (6): either player skips them for both (the pads come from the session,
    // the same on both sides) - user: "musi jit preskakovat replay a nejlip i zacatek ty uvodni sceny"
    const uint32_t gm = T6GameMode();
    st[5] &= (gm <= 2 || gm == 6) ? ~0x20 : ~0x30;
    // The intros are never skipped by themselves (user 2026-10-13: "lidi si je muzou preskocit nebo se na ne podivat").
    // A player's START in game modes 0-2 counts for both users: with user 0 alone the first part, mode 0, went in
    // 0.2 s but mode 1 ran its full 5.9 s (rm1). Both pads come from the session = the same frame on both sides.
    if (gm <= 2 && ((g_net_in[0][1] | g_net_in[1][1]) & 0x10)) st[5] |= 0x10;
    // 'skipintro=1' (tests only, T6_NET_SKIPINTRO / start line; the launcher does not send it): START pulses of both
    // users in game modes 0-2, decided by the session frame and the game mode only.
    if (net_config().skip_intro && gm <= 2 && (g_net_frame & 7) < 2) st[5] |= 0x10;
    ctx.r3.u64 = 0;
    return;
  }
  // Users 2 / 3 do not exist in a netplay run: no host poll (with no controller plugged in the mmjoystick poll of the
  // missing devices cost 16 ms per pad step - every live and every re-simulated frame)
  if (net_config().enabled && ctx.r3.u32 >= 2) {
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
    return;
  }
  const uint32_t physicalUser=ctx.r3.u32,physicalState=ctx.r4.u32;
  const double h0 = T6NowMs();
  __imp__sub_826B58E8(ctx, base);
  g_pad_host_ms += T6NowMs() - h0;
  g_pad_host_n++;
  if(ctx.r3.u32==0)T6PositionSnapshotPad(physicalUser,base+physicalState);
}

// XInputGetCapabilities(user r3, flags r4, caps* r5) of the game (sub_826B58E0, overrides the weak symbol). The game
// only reads the pads of connected users: with only the local stick connected, player 2's session inputs were never read
// (v1-v26: P2 stood still). While the inputs come from the session / the menu driver, users 0 and 1 are always connected
// gamepads and users 2 / 3 disconnected, the same on both instances whatever controllers the PCs have.
extern "C" void __imp__sub_826B58E0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_826B58E0(PPCContext& ctx, uint8_t* base) {
  static const bool ours = net_config().enabled || T6Env("T6_AUTO_CONFIRM") != nullptr || T6Env("T6_PRACTICE") != nullptr;
  if (!ours) {
    __imp__sub_826B58E0(ctx, base);
    return;
  }
  if (ctx.r3.u32 >= 2) {
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
    return;
  }
  uint8_t* c = base + ctx.r5.u32;  // XINPUT_CAPABILITIES: Type, SubType, Flags (BE u16), Gamepad (12), Vibration (4)
  std::memset(c, 0, 20);
  c[0] = 1;  // XINPUT_DEVTYPE_GAMEPAD
  c[1] = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
  c[4] = 0xFF;  // all buttons, triggers, sticks present
  c[5] = 0xFF;
  c[6] = 0xFF;
  c[7] = 0xFF;
  std::memset(c + 8, 0xFF, 8);
  ctx.r3.u64 = 0;
}



// XInputSetState (sub_826B58F8: host XamInputGetCapabilities + XamInputSetState = rumble) and XInputGetKeystroke
// (sub_826B59C0 -> XamInputGetKeystrokeEx): the game calls them in every pad step. With no controller plugged in the
// host calls (mmjoystick backend) took 16 ms per pad step - every live frame missed its vblank (30 fps) and every
// re-simulated frame cost 16 ms more. In a netplay run the pads are virtual: no host call, timing equal on every PC.
// The rumble of the local player is lost for now.
extern "C" void __imp__sub_826B58F8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_826B59C0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_826B58F8(PPCContext& ctx, uint8_t* base) {
  // T6_NO_VIBRATION=1 (launcher option): no rumble anywhere (practice and offline play have it otherwise)
  static const bool no_vibration = T6Env("T6_NO_VIBRATION") != nullptr;
  if (!net_config().enabled && !no_vibration) {
    __imp__sub_826B58F8(ctx, base);
    return;
  }
  ctx.r3.u64 = ctx.r3.u32 < 2 ? 0 : 1167;  // ERROR_SUCCESS / ERROR_DEVICE_NOT_CONNECTED
}
extern "C" void sub_826B59C0(PPCContext& ctx, uint8_t* base) {
  if (!net_config().enabled) {
    __imp__sub_826B59C0(ctx, base);
    return;
  }
  ctx.r3.u64 = 4306;  // ERROR_EMPTY: no keystroke
}
