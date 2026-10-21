// Tekken 6 netplay - hold point and match start (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Hold point: the game is already running when the match is made (user: "hra je zapla, v launcheru najde hru a
// hra skoci do loadingu") ----
// T6_NET_HOLD=1 (with T6_NET=1 and T6_PLAY=tools/menu_vs.bin): the menu path is replayed up to pad step T6_NET_HOLD_STEP
// (default 2640 = 6 steps before the last confirm of the stage select, 2646; the battle is created at 2796) and the main
// thread parks at the frame boundary: no frame runs, so both instances stand in exactly the same state however long
// each of them waits. The status file says 'state ready'. The launcher then writes one line to T6_NET_CMD:
//   start player=1 port=7000 remote=1.2.3.4:7001 delay=1 seed=123 char1=3 char2=33 stage=5 rounds=3 [time=60] [input=live] [name1=<hex> name2=<hex>] [rank1=<n> rank2=<n>] [custom1=<160 hex> custom2=<160 hex>]
// The values replace the environment ones (T6_NET_*, T6_CHAR_P1/P2, T6_STAGE, T6_ROUNDS, T6_ROUND_TIME) and the replay
// goes on: last confirm -> fight loading -> session start as before. 'quit' ends the process.
void T6NetHold() {
  static const bool on = T6Env("T6_NET_HOLD") != nullptr && net_config().enabled;
  static bool done = false;
  static const uint32_t hold_step = T6EnvU32("T6_NET_HOLD_STEP", 2640, 0);
  // Fight player objects (*(0x828DCE88), *(0x828DCE8C), 0xC40 bytes each): the battle init does not write every word,
  // a game that has played keeps what its last match left (hit positions, counters, pointers to objects of the old
  // match). The list of such words cleared at the session start (StartSession) came from one match and is not
  // complete: fresh_vs_parked 2026-10-13, another match - 3 + 10 words differed for the whole second match (+0x170 a
  // pointer of the old match, +0x3BC.., +0x734, +0x750.., +0xBE0), third RNG and result equal. Now every word that is
  // 0 in a fresh game at its hold point is 0 again at the hold point after a match: the battle init starts from
  // the same bytes on a parked and on a fresh game.
  static bool fresh_zero[2][0xC40 / 4];
  static bool fresh_have = false;
  const auto player_words = [&](bool note) {
    uint32_t cleared = 0;
    for (int i = 0; i < 2; i++) {
      const uint32_t pl = T6PlayerPtr(i);
      if (!T6PlayerObjectOk(pl)) continue;
      for (uint32_t o = 0; o < 0xC40; o += 4) {
        uint32_t& w = *reinterpret_cast<uint32_t*>(g_t6_membase + pl + o);
        if (note) {
          fresh_zero[i][o / 4] = w == 0;
        } else if (fresh_zero[i][o / 4] && w != 0) {
          w = 0;
          cleared++;
        }
      }
    }
    return cleared;
  };
  // (the fresh game's words are noted at the hold step also without T6_NET_HOLD: tests without a launcher)
  if (!fresh_have && !g_park && state().step == hold_step) {
    player_words(true);
    fresh_have = true;
  }
  const bool first = on && !done && state().step == hold_step;
  // back at the stage select after a match (park): the same wait for 'start'
  const bool again = g_park && !g_park_held && g_park_t1 && state().step - g_park_t1 == kParkHold;
  if (!first && !again) return;
  if (first) done = true;
  if (again) {
    g_park_held = true;
    g_net_fast_over = true;
    if (fresh_have) REXLOG_INFO("T6 park: {} words of the last match cleared in the fight player objects (0 in a fresh game)", player_words(false));
    REXLOG_INFO("T6 park: parked at pad step {} (+{}; battle task gone at +{})", state().step, state().step - g_park_step0, g_park_t1 - g_park_step0);
    if (!on) {  // test without a launcher (net_x360.ps1 + T6_NET_AUTOPARK): the next match starts at once, same peer
      g_park_started = true;
      g_park_confirm = state().step;
      return;
    }
  }
  const char* status = T6Env("T6_NET_STATUS");
  const char* cmd = T6Env("T6_NET_CMD");
  const auto write_status = [&](const std::string& text) {
    if (status && *status) T6WriteFileAtomic(status, text);
  };
  REXLOG_INFO("T6 net: hold at pad step {}: waiting for 'start' in {}", hold_step, cmd ? cmd : "(T6_NET_CMD not set)");
  // Diagnostics (T6_NET_DUMP_DIR): hash of every committed page of the XEX data/bss and the game heaps at the hold
  // point -> hold_pages.txt; compared between instances / runs to see which pages are equal for everybody.
  if (const char* dir = T6Env("T6_NET_DUMP_DIR"); dir && *dir) {
    if (FILE* f = T6Open(fmt::format("{}/hold_pages.txt", dir), "w")) {
      for (const auto& [lo, hi] : kT6GameRanges) {
        ForEachCommitted(lo, hi, [&](uint32_t a0, uint32_t n) {
          for (uint32_t q = a0; q + 0x1000 <= a0 + n; q += 0x1000) {
            std::fprintf(f, "%08X %016llX\n", q, static_cast<unsigned long long>(Fnv(g_t6_membase + q, 0x1000)));
          }
        });
      }
      std::fclose(f);
    }
    // T6_HOLD_IMAGE=1: the XEX image's read-only data and code (0x82000000-0x82726000) -> image.bin (vtables / RTTI
    // for reverse engineering; the file on disk is compressed)
    if (T6Env("T6_HOLD_IMAGE")) {
      if (FILE* f = T6Open(fmt::format("{}/image.bin", dir), "wb")) {
        std::fwrite(g_t6_membase + 0x82000000u, 1, 0x726000, f);
        std::fclose(f);
      }
    }
    // raw bytes of the pages that made up the second boot variant (park run 6: 82984000 XAudio2 engine, heap A7342000,
    // A748F000) -> hold_raw.bin (u32 address + 4096 bytes each), to see what they hold when it happens again
    if (FILE* f = T6Open(fmt::format("{}/hold_raw.bin", dir), "wb")) {
      for (const uint32_t q : {0x82984000u, 0xA7342000u, 0xA748F000u,
                             0x827D8000u, 0x827D9000u}) {  // (+ the page the odd state hashes differ in, 2026-10-13, and its neighbour)
        if (!PageReadable(q >> 12)) continue;
        std::fwrite(&q, 4, 1, f);
        std::fwrite(g_t6_membase + q, 1, 0x1000, f);
      }
      std::fclose(f);
    }
  }
  // State hash for the launcher / lobby server (both players must stand in the same state: same build, same reference
  // profile, same menu path). Of 20106 committed pages at the hold point 24 differ between instances / runs (hp1, hp2):
  // file request queue, job pool control, thread objects, sound statics and 11 heap pages. 'statehash' = XEX data/bss
  // without those and without the ranges known to vary in a session (enforced by the server); 'heaphash' = game heaps
  // without the 11 pages seen to vary (informative until more runs show it is stable).
  uint64_t state_hash = 0xcbf29ce484222325ull, heap_hash = 0xcbf29ce484222325ull;
  {
    static const Range skip[] = {{0x827A7000u, 0x827A8000u}, {0x827BF000u, 0x827C0000u}, {0x827D5000u, 0x827D7000u},
                                 {0x827F8000u, 0x827FC000u}, {0x827FD000u, 0x827FF000u}, {0x82829000u, 0x8282A000u},
                                 {0x828B3000u, 0x828B4000u}, {0x8297F000u, 0x82980000u}, {0x829C9000u, 0x829F1000u},
                                 {0x82984000u, 0x82985000u},
                                 // 827D8000: the one XEX data page in which the boot variants 34086BEA.. / 552C1E2C.. differ from
                                 // D7B68FFE.. (kept dumps, 2026-10-13). Which variant a start gives depends on its timing; games of
                                 // two different variants stayed in sync over whole matches (package test: 5915 + 6427 frames, 0
                                 // differ), so it must not raise the "different game state" warning.
                                 {0x827D8000u, 0x827D9000u},  // XAudio2 engine object (processing thread): second boot variant, park run 6
                                 {0x829F4000u, 0x829F5000u}, {0x82C65000u, 0x82C67000u}, {0x82CDE000u, 0x82CEA000u},  // sound library statics (82CE2000 / 82CE3000 varied too: sh1, sh2; 82CDE000 between two PCs)
                                 {0xA7854000u, 0xA7855000u}, {0xA7869000u, 0xA786A000u}, {0xA7874000u, 0xA7875000u},
                                 {0xA787D000u, 0xA787E000u}, {0xA7882000u, 0xA7883000u}, {0xAA0F2000u, 0xAA0F3000u},
                                 {0xAB373000u, 0xAB374000u}, {0xAB802000u, 0xAB803000u}, {0xAB804000u, 0xAB805000u},
                                 {0xAB926000u, 0xAB928000u}, {0xAB9F9000u, 0xAB9FA000u}};
    const auto skipped = [&](uint32_t q) {
      for (const auto& r : skip) if (q >= r.lo && q < r.hi) return true;
      return false;
    };
    for (const auto& [lo, hi] : kT6GameRanges) {
      uint64_t& h = lo < 0xA0000000u ? state_hash : heap_hash;
      ForEachCommitted(lo, hi, [&](uint32_t a0, uint32_t n) {
        for (uint32_t q = a0; q + 0x1000 <= a0 + n; q += 0x1000) {
          if (!skipped(q)) h = (h ^ FnvNoHandles(g_t6_membase + q) ^ q) * 0x100000001b3ull;
        }
      });
    }
  }
  // A game parked again after a match reports the hashes of its first hold point: its memory is not that of a fresh
  // game any more (the server would warn about every such player), and the comparison only says something about the
  // build / profile / menu path, which the first hold point has already shown.
  {
    static uint64_t first_state = 0, first_heap = 0;
    if (first) { first_state = state_hash; first_heap = heap_hash; }
    else { REXLOG_INFO("T6 park: state hash now {:016X} / {:016X}, reported as at the first hold point", state_hash, heap_hash); state_hash = first_state; heap_hash = first_heap; }
  }
  // (the 156 steps from here to the session start - stage confirm, fade - are not shown either: they run fast too,
  // 2.6 s less per match start; the fast start ends at the session start. Was: real time from the start command.)
  // T6_HOLD_DUMP=1 (diagnostics): XEX data / bss at every hold point -> <dump dir>/hold_data_<n>.bin (fresh boot = 1,
  // after a park = 2, ...): what a game parked after a match keeps that a fresh one does not (result menu choice)
  if (T6Env("T6_HOLD_DUMP")) {
    static int n = 0;
    const char* d = T6Env("T6_NET_DUMP_DIR");
    if (FILE* f = T6Open(fmt::format("{}/hold_data_{}.bin", d ? d : ".", ++n), "wb")) {
      std::fwrite(g_t6_membase + 0x82726000u, 1, 0x82DB0000u - 0x82726000u, f);
      std::fclose(f);
    }
  }
  // The page hashes / raw pages of every DIFFERENT state hash are kept (hold_pages_<hash>.txt, hold_raw_<hash>.bin,
  // once per hash): the odd boot of 2026-10-13 (34086BEA.., once in 200 hold points) was overwritten by the next run
  // before anyone could compare its pages.
  if (const char* dir = T6Env("T6_NET_DUMP_DIR"); first && dir && *dir) {
    for (const auto& [from, to] : {std::pair{"hold_pages.txt", fmt::format("hold_pages_{:016X}.txt", state_hash)},
                                   std::pair{"hold_raw.bin", fmt::format("hold_raw_{:016X}.bin", state_hash)}}) {
      CopyFileA(fmt::format("{}/{}", dir, from).c_str(), fmt::format("{}/{}", dir, to).c_str(), TRUE);  // (TRUE: never over an existing one)
    }
  }
  REXLOG_INFO("T6 net: hold point state hash {:016X}, heap hash {:016X}", state_hash, heap_hash);
  write_status(fmt::format("state ready\nstatehash {:016X}\nheaphash {:016X}\n", state_hash, heap_hash));
  g_t6_holding = true;
  const double t0 = T6NowMs();
  std::string line;
  for (;;) {
    if (cmd && *cmd) {
      if (FILE* f = T6Open(cmd, "r")) {
        char buf[2048] = {};  // (custom1= / custom2=: 160 hex digits each)
        const bool got = std::fgets(buf, sizeof(buf), f) != nullptr;
        std::fclose(f);
        if (got) {
          std::remove(cmd);
          line = buf;
          if (line.rfind("quit", 0) == 0) {
            REXLOG_INFO("T6 net: quit command at the hold point");
            ExitProcess(0);
          }
          if (line.rfind("start", 0) == 0) break;
          REXLOG_WARN("T6 net: unknown command at the hold point: {}", line);
        }
      }
    }
    Sleep(15);
  }
  g_t6_holding = false;
  if (again) {
    g_park_started = true;
    g_park_confirm = state().step;
  }
  NetConfig& c = net_config_mut();
  // (a start line without rank1= / rank2= must not show the ranks of the match before)
  T6SetParam("T6_RANK_P1", "");
  T6SetParam("T6_RANK_P2", "");
  T6SetParam("T6_PROMO", "");  // (only the game that decides a set with a promotion has promo=)
  // (the same for the custom looks: a start line without them = the costume's normal look)
  T6SetParam("T6_CUSTOM_P1", "");
  T6SetParam("T6_CUSTOM_P2", "");
  size_t pos = 5;
  while (pos < line.size()) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\r' || line[pos] == '\n' || line[pos] == '\t')) pos++;
    const size_t end = line.find_first_of(" \r\n\t", pos);
    const std::string tok = line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    pos = end == std::string::npos ? line.size() : end;
    const size_t eq = tok.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
    if (k == "player") c.player = T6ToInt(v.c_str()) == 2 ? 2 : 1;
    else if (k == "port") c.local_port = static_cast<uint16_t>(T6ToInt(v.c_str()));
    else if (k == "delay") c.delay = T6ToInt(v.c_str());
    else if (k == "skipintro") c.skip_intro = v == "1";
    else if (k == "seed") c.seed = std::strtoul(v.c_str(), nullptr, 0);
    else if (k == "input") { c.random_input = v == "random"; c.attack_input = v == "attack"; c.step_input = v == "step"; }
    else if (k == "keys") T6SetParam("T6_NET_KEYS", v.c_str());
    else if (k == "remote") c.SetRemote(v);
    else if (k == "char1") T6SetParam("T6_CHAR_P1", v.c_str());
    else if (k == "char2") T6SetParam("T6_CHAR_P2", v.c_str());
    else if (k == "stage") T6SetParam("T6_STAGE", v.c_str());
    else if (k == "rounds") T6SetParam("T6_ROUNDS", v.c_str());
    else if (k == "time") T6SetParam("T6_ROUND_TIME", v.c_str());
    else if (k == "rank1") T6SetParam("T6_RANK_P1", v.c_str());  // rank byte of the HUD plate of side 1 / 2 (t6_watch.cpp)
    else if (k == "rank2") T6SetParam("T6_RANK_P2", v.c_str());
    else if (k == "promo") T6SetParam("T6_PROMO", v.c_str());  // rank this player is promoted to by winning the match
    // custom look of side 1 / 2 (the 80-byte record of the game's Customize menu, 160 hex digits; t6_watch.cpp
    // sub_82244368 returns it for that side) - both games get both, so both inject the same
    else if ((k == "custom1" || k == "custom2") && v.size() == 160 && v.find_first_not_of("0123456789ABCDEFabcdef") == std::string::npos)
      T6SetParam(k == "custom1" ? "T6_CUSTOM_P1" : "T6_CUSTOM_P2", v.c_str());
    else if (k == "name1" || k == "name2") {  // player names for the HUD name plates, hex of the bytes (t6_watch.cpp)
      std::string n;
      for (size_t i = 0; i + 1 < v.size(); i += 2) n += static_cast<char>(std::strtoul(v.substr(i, 2).c_str(), nullptr, 16));
      T6SetParam(k == "name1" ? "T6_NAME_P1" : "T6_NAME_P2", n.c_str());
    }
    else REXLOG_WARN("T6 net: unknown start parameter '{}'", tok);
  }
  // The match setup object (sub_8238E3B8: +352 stage, +356 / +360 character codes) still holds the two characters of
  // the menu recording (LEE / DRAGUNOV): the VS loading screen wrote those names over the right models (user
  // 2026-10-13; the models come from the player setup hook, t6_watch.cpp). The codes of this match are written here,
  // before the last confirm of the stage select = before the loading screen exists; the same on both sides.
  {
    uint32_t mi = 0;
    T6StatusMatchInfo(&mi);
    const auto put = [&](uint32_t off, const char* env) {
      const char* v = T6Env(env);
      if (!mi || !v || !*v || T6ToInt(v) < 0) return;
      *reinterpret_cast<uint32_t*>(g_t6_membase + mi + off) = _byteswap_ulong(std::strtoul(v, nullptr, 0));
    };
    put(356, "T6_CHAR_P1");
    put(360, "T6_CHAR_P2");
    put(352, "T6_STAGE");
    REXLOG_INFO("T6 net: match setup {:08X}: characters {} / {}, stage {} written for the loading screen", mi,
                mi ? T6Rd32(mi + 356) : 0, mi ? T6Rd32(mi + 360) : 0, mi ? T6Rd32(mi + 352) : 0);
  }
  // Mokujin's styles (mk13, 2026-10-16): the setup object's table of his style per round (+220, read by sub_8238DF18
  // between the rounds) is filled HERE, in the steps between the start command and the session - before StartSession
  // syncs the RNGs - so each side drew its own (6 23 9 against 6 4 16: the 2nd round already differed). Only in a
  // Mokujin match (user 2026-10-15: "jen mokujina"): the main thread's CRT rand state and the main LCG get values from
  // the agreed seed now, the same on both sides. T6_NET_CRT_SYNC=0 = off.
  {
    static const bool off = T6EnvIs("T6_NET_CRT_SYNC", '0');
    if (!off && T6MokuMatch()) {
      uint32_t h = c.seed ^ 0x4D4F4B55u;  // ("MOKU")
      h ^= h >> 16;
      h *= 0x85EBCA6Bu;
      h ^= h >> 13;
      if (const uint32_t ptd = CrtPtd()) *reinterpret_cast<uint32_t*>(g_t6_membase + ptd + 20) = _byteswap_ulong(h);
      *reinterpret_cast<uint32_t*>(g_t6_membase + 0x828B49E0u) = _byteswap_ulong(c.seed);
      REXLOG_INFO("T6 net: Mokujin in this match - rand() and the main LCG set from the seed before his styles are drawn");
    }
  }
  g_net_fast_over = false;  // fast up to the session start (StartSession sets it again)
  REXLOG_INFO("T6 net: start after {:.1f} s at the hold point: player {}, port {}, remote {}:{}, delay {}, seed {}, input {}",
              (T6NowMs() - t0) / 1000.0, c.player, c.local_port, c.remote_ip, c.remote_port, c.delay, c.seed, c.random_input ? "random" : "live");
  write_status("state loading\n");
}

// Motion blur off (T6_NO_BLUR=1, launcher option; user 2026-10-12: the space around a dashing character is smeared).
// tcb_BlurControl (vtable 82003E60, tools/vtable_of.py; ctor sub_821C6648, step sub_821C6738). Its readers ask
// sub_821C6570(this) = "blur active" (+88 when +84 and +180 are set) and sub_821C6598(this, i) = the flag of entry i
// (+224 + 4i). With the option both answer 0; the task's own memory is NOT touched (setting its "off" request +96
// changed the state hash of the hold point: a player with blur off against one with blur on got the "different game
// state" warning, nb1).
extern "C" void __imp__sub_821C6570(PPCContext& ctx, uint8_t* base);
extern "C" void sub_821C6570(PPCContext& ctx, uint8_t* base) {
  static const bool off = T6Env("T6_NO_BLUR") != nullptr;
  __imp__sub_821C6570(ctx, base);
  if (off && ctx.r3.u32) {
    if (static bool logged = false; !logged) {
      logged = true;
      REXLOG_INFO("T6 blur: motion blur off (the game asked for it at pad step {})", state().step);
    }
    ctx.r3.u64 = 0;
  }
}
extern "C" void __imp__sub_821C6598(PPCContext& ctx, uint8_t* base);
extern "C" void sub_821C6598(PPCContext& ctx, uint8_t* base) {
  static const bool off = T6Env("T6_NO_BLUR") != nullptr;
  __imp__sub_821C6598(ctx, base);
  if (off) ctx.r3.u64 = 0;
}

// Stage music off (T6_NO_MUSIC=1, launcher option; user 2026-10-15: "aby se dala vypnout stage music aby lidi mohli mit
// treba vlastni"). The Xbox 360 way: the system's music category volume, the one the console sets while the player
// plays his own music. The SDK export always answers 1.0; with the option category 1 (background music) answers 0.
// Its reader is the render driver below (sub_824EFC58, change mask); sub_8267BF10 (XAudio2 engine +524) asks it too
// but never ran in a practice fight (mus1, 2026-10-19). Only the volume of the music changes: the sound library's
// memory is live, not in the snapshot / state hash, the game logic never reads it.
extern "C" void __imp__XAudioGetVoiceCategoryVolume(PPCContext& ctx, uint8_t* base) {
  static const bool no_music = T6Env("T6_NO_MUSIC") != nullptr;
  const uint32_t category = ctx.r3.u32, out = ctx.r4.u32;
  float v = no_music && category == 1 ? 0.0f : 1.0f;
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  if (out)
    *reinterpret_cast<uint32_t*>(base + out) = _byteswap_ulong(bits);
  if (static bool logged = false; !logged && no_music && category == 1) {
    logged = true;
    REXLOG_INFO("T6 music: stage music off (category {} volume 0)", category);
  }
  ctx.r3.u64 = 0;
}

// The render driver (sub_824EFC58, ~190 times a second) asks for a category's volume only when the change mask has
// the category's bit (bit 0 = category 0, bit 1 = category 1 music) - the console sets it when the player starts his
// own music. The SDK always answers 0, so the 0 above was never asked for: 2.2.0-2.3.0 played the music anyway (user
// 2026-10-19). With T6_NO_MUSIC the music bit is set every pass. Measured (practice, stage 1, output RMS): music on
// 0.031, off 0.009 (the stage sounds and effects stay).
extern "C" void __imp__XAudioGetVoiceCategoryVolumeChangeMask(PPCContext& ctx, uint8_t* base) {
  static const bool no_music = T6Env("T6_NO_MUSIC") != nullptr;
  SwitchToThread();  // (the SDK export's yield: the driver polls it in a loop)
  if (const uint32_t out = ctx.r4.u32)
    *reinterpret_cast<uint32_t*>(base + out) = _byteswap_ulong(no_music ? 2u : 0u);
  ctx.r3.u64 = 0;
}

// sub_821BA860 (stage objects: rand() % 100 + 1 picks one of their weighted choices) with its own rand state in a
// netplay session (see CrtRandSync): the shared CRT state is swapped out around it.
extern "C" void __imp__sub_821BA860(PPCContext& ctx, uint8_t* base);
extern "C" void sub_821BA860(PPCContext& ctx, uint8_t* base) {
  static const bool off = T6EnvIs("T6_NET_CRT_SYNC", '0');
  const uint32_t ptd = (off || !g_moku_match || !g_net_running || t6_thread_bit != 0) ? 0 : CrtPtd();
  if (!ptd) {
    __imp__sub_821BA860(ctx, base);
    return;
  }
  static uint32_t own = 0x2545F491u;
  uint32_t* st = reinterpret_cast<uint32_t*>(base + ptd + 20);
  const uint32_t shared = *st;
  *st = own;
  __imp__sub_821BA860(ctx, base);
  own = *st;
  *st = shared;
}

// Mokujin's style (mk11, 2026-10-16: the only writer of fight player +0x54 is sub_822F42A8, the player's character
// data from the table 0x82744AA8 by +0x5C; for character 18 = Mokujin the style comes from the match setup object's
// virtual +80). T6_MOKU_DIAG=1: that call's target, arguments, result and the RNG states around it.
extern "C" void __imp__sub_822F42A8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8238E3B8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822F42A8(PPCContext& ctx, uint8_t* base) {
  static const bool diag = T6Env("T6_MOKU_DIAG") != nullptr;
  if (!diag) {
    __imp__sub_822F42A8(ctx, base);
    return;
  }
  const uint32_t pl = ctx.r3.u32, lr = static_cast<uint32_t>(ctx.lr);
  PPCContext tmp = ctx;
  __imp__sub_8238E3B8(tmp, base);
  const uint32_t setup = tmp.r3.u32, vt = setup ? T6Rd32(setup) : 0, fn = vt ? T6Rd32(vt + 80) : 0;
  const uint32_t crt = CrtPtd() ? T6Rd32(CrtPtd() + 20) : 0, lcg = T6Rd32(0x828B49E0u);
  __imp__sub_822F42A8(ctx, base);
  // the style table of the setup object (sub_8238DF18: u16 at +2 * (110 + side * 9 + round), +341 = rounds)
  std::string table;
  for (uint32_t i = 0; setup && i < 18; i++)
    table += fmt::format("{}{}", i == 9 ? " / " : " ", _byteswap_ushort(*reinterpret_cast<const uint16_t*>(base + setup + 220 + 2 * i)));
  REXLOG_INFO("T6 moku: table{}", table);
  REXLOG_INFO("T6 moku: frame {} player {:08X} char {} index {} style {} | setup {:08X} vt {:08X} +80 = sub_{:08X} r4 from +76 {} r5 {:08X} | "
              "CRT {:08X} -> {:08X}, LCG {:08X} -> {:08X}, lr {:08X}",
              g_net_frame, pl, T6Rd32(pl + 88), T6Rd32(pl + 92), T6Rd32(pl + 84), setup, vt, fn, T6Rd32(pl + 76), T6Rd32(0x828CCF54u), crt,
              CrtPtd() ? T6Rd32(CrtPtd() + 20) : 0, lcg, T6Rd32(0x828B49E0u), lr);
}

// Mokujin's style in every round (user 2026-10-16: "ma mit kazde kolo random a maji to mit oba stejne"). The match
// setup object's virtual sub_8238DF50(setup, side) draws his styles for the rounds count at +341 only (3 = the menu's
// rounds, sub_822F3BC0 = rand() % 39 -> table), and the getter sub_8238DF18(setup, side, round) gives round >= +341 the
// style of round 1 - first to 3 had rounds 4 / 5 always in the style of round 1. The drawer now draws the rounds up to
// 9 (the table's size per side) too, with the same rand() right after it (synced by the start command in netplay), and
// the getter gives those rounds their own style. Kept on the host (not in guest memory), per side, set by every draw.
namespace t6np {
uint32_t g_moku_setup[2] = {0, 0};  // setup object whose Mokujin styles of rounds +341..8 are in g_moku_extra (0 = none)
uint16_t g_moku_extra[2][9];
int g_moku_drawn[2] = {0, 0};  // rounds the game itself drew = +341 at the draw (the game raises +341 later - mok1
                               // 2026-10-17: rounds 4 / 5 then read the table's empty entries = style 0 = PAUL)
uint32_t MokuCharId(uint32_t setup, uint32_t side) {  // sub_822F39F8 of the side's character code
  const int32_t code = static_cast<int32_t>(T6Rd32(setup + (89 + side) * 4));
  return T6Rd32(0x82744AA8u + (code >= 0 && code < 102 ? code * 108 : 0));
}
}  // namespace t6np

extern "C" void __imp__sub_8238DF50(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_822F3BC0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8238DF50(PPCContext& ctx, uint8_t* base) {
  const uint32_t setup = ctx.r3.u32, side = ctx.r4.u32;
  PPCContext tmp = ctx;
  __imp__sub_8238DF50(ctx, base);
  if (side > 1) return;
  g_moku_setup[side] = 0;
  if (MokuCharId(setup, side) != 18) return;
  const int rounds = T6Signed8(base[setup + 341]);
  std::string drawn;
  for (int i = rounds < 0 ? 0 : rounds; i < 9; i++) {
    __imp__sub_822F3BC0(tmp, base);
    g_moku_extra[side][i] = static_cast<uint16_t>(tmp.r3.u32);
    drawn += fmt::format(" {}", g_moku_extra[side][i]);
  }
  g_moku_setup[side] = setup;
  g_moku_drawn[side] = rounds < 0 ? 0 : rounds;
  REXLOG_INFO("T6 moku: side {} rounds {} - styles of the rounds after them:{}", side, rounds, drawn);
}

extern "C" void __imp__sub_8238DF18(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8238DF18(PPCContext& ctx, uint8_t* base) {
  const uint32_t setup = ctx.r3.u32, side = ctx.r4.u32;
  const int32_t round = static_cast<int32_t>(ctx.r5.u32);
  if (side <= 1 && g_moku_setup[side] == setup && round >= g_moku_drawn[side] && round < 9 && MokuCharId(setup, side) == 18) {
    ctx.r3.u64 = g_moku_extra[side][round];
    return;
  }
  __imp__sub_8238DF18(ctx, base);
}

// The game's own rank up display (NOTES "The game's own rank up display"): the battle task sub_822FAFE0 gives, at the
// winner's pose of a ranked game, its hud object (task +156 -> +260) +140 = 1, +144 = the new rank (sub_822FCBA0) =
// "Promoted to <rank>". Our matches have that hud but never the game's own ranks, so (user 2026-10-16: "delame jen
// promotion", "jen na ty kdo vyhral", "ma se to stat na konci hry") this game's player gets it at the KO that wins him
// the match, when the server said that win promotes him (start line promo=<rank> -> T6_PROMO; T6_TEST_PROMO=<rank>:
// tests, every match). The game keeps it on the screen while +140 is set: cleared when the next round / match starts.
// Only this side's hud changes (the other player sees nothing); the players hash and the RNGs are not touched.
namespace t6np {
uint32_t AliveRef(uint32_t p) {  // the game's weak reference at p (object, then its serial at p + 4)
  const uint32_t o = T6Rd32(p);
  return o && T6Rd32(o + 12) == T6Rd32(p + 4) && (T6Rd32(o + 60) & T6Rd32(0x82007560u)) ? o : 0;
}
}  // namespace t6np
extern "C" void __imp__sub_822FAFE0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822FAFE0(PPCContext& ctx, uint8_t* base) {
  const uint32_t task = ctx.r3.u32;
  __imp__sub_822FAFE0(ctx, base);
  static uint32_t given = 0, given_rank = 0;  // hud that has our promotion (cleared again before the next round / match)
  const uint32_t mode = T6GameMode();
  if (mode < 6 || mode > 9) {
    if (given) {
      if (T6Rd32(given + 140) == 1) {
        *reinterpret_cast<uint32_t*>(base + given + 140) = 0;
        t6_dirty_mark(given + 140);
      }
      given = 0;
    }
    return;
  }
  // (review 2026-10-18) the writes are marked for the rollback snapshots, and a rollback to a frame before them (KO
  // grace frames predict) takes the display away again: written once more while the match end lasts
  if (given) {
    if (T6Rd32(given + 140) != 1 && AliveRef(task + 156) && AliveRef(AliveRef(task + 156) + 260) == given) {
      *reinterpret_cast<uint32_t*>(base + given + 144) = _byteswap_ulong(given_rank);
      *reinterpret_cast<uint32_t*>(base + given + 140) = _byteswap_ulong(1u);
      t6_dirty_mark(given + 140);
      t6_dirty_mark(given + 144);
    }
    return;
  }
  if (mode != 6) return;
  const char* env = T6Env("T6_PROMO");
  if (!env || !*env) env = T6Env("T6_TEST_PROMO");
  if (!env || !*env) return;
  const int rank = T6ToInt(env);
  if (rank <= 0 || rank > 40) return;
  // this game's player won the match with this KO: his round wins reached the rounds to win
  const uint32_t me = net_config().player == 2 ? 1 : 0;
  const uint32_t pme = T6PlayerPtr(me), pother = T6PlayerPtr((1 - me));
  if (!pme || !pother) return;
  const uint32_t wins = T6Rd32(pme + 112), other = T6Rd32(pother + 112);
  const char* rv = T6Env("T6_ROUNDS");
  const uint32_t need = rv && T6ToInt(rv) >= 1 ? static_cast<uint32_t>(T6ToInt(rv)) : 3;
  if (wins < need || wins <= other) return;
  const uint32_t o = AliveRef(task + 156), hud = o ? AliveRef(o + 260) : 0;
  if (!hud) {
    REXLOG_INFO("T6 promo: no hud object at the final KO (frame {})", g_net_frame);
    return;
  }
  *reinterpret_cast<uint32_t*>(base + hud + 144) = _byteswap_ulong(static_cast<uint32_t>(rank));
  *reinterpret_cast<uint32_t*>(base + hud + 140) = _byteswap_ulong(1u);
  t6_dirty_mark(hud + 140);
  t6_dirty_mark(hud + 144);
  given = hud;
  given_rank = static_cast<uint32_t>(rank);
  REXLOG_INFO("T6 promo: promoted to rank {} shown (rounds {}:{}, frame {})", rank, wins, other, g_net_frame);
}

// share of slow fight frames of the current game in percent (g_slow_frames), -1 before 600 fight frames (10 s)
int T6NetSlowPct() { return g_fight_frames >= 600 ? static_cast<int>(g_slow_frames * 100 / g_fight_frames) : -1; }
// live fight frames with a predicted remote input in percent, -1 before 600 fight frames
int T6NetLagPct() { return g_fight_frames >= 600 ? static_cast<int>(g_lag_pred * 100 / g_fight_frames) : -1; }
// frames the peer was behind on average, x 10 (45 = 4.5 frames), -1 before 600 fight frames
int T6NetBehind10() { return g_fight_frames >= 600 && g_behind_n ? static_cast<int>(g_behind_sum * 10 / g_behind_n) : -1; }
// the ping of those samples on average in ms, -1 before 600 fight frames
int T6NetPingAvg() { return g_fight_frames >= 600 && g_behind_n ? static_cast<int>(g_ping_sum / g_behind_n) : -1; }
