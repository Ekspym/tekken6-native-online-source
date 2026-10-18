// Match status for the launcher (T6_NET_STATUS) and launcher commands (T6_NET_CMD).
//
// Game mode state (PS3 0x00B7FBEC, records\analysis_game_modes.md of the PS3 project): X360 0x8292E660, +0xC current
// mode (getter sub_822CD6F8), +0x10 next mode, +0x24 frame counter of the phase; the per-mode attribute table is at
// 0x82010B90 (identical to PS3 0x00842880). sub_822CD6E0 commits next -> current at the start of the battle's per-frame
// update (PS3 FUN_0020c744); it was inlined, so the getter sub_822CD6F8 is overridden (weak generated symbol).
// Modes: 0-2 intros, 3 "Round N ... Fight!", 4 fight, 5 after KO, 6 KO replay, 10 between rounds, 7/8/9 end of the
// match (win pose), 11/12/25 continue / results / exit, 13-24 other game types.
//
// T6_NET_STATUS=<file>: rewritten on every change: 'state <name>', 'mode <n>', 'round <p1 wins> <p2 wins>', 'roundno <n>',
//   'winner <0|1|2>' (match end, 0 = draw), 'disconnected' (T6StatusDisconnected from the netplay code).
// T6_NET_CMD=<file>: polled every 30 frames: 'quit' ends the process, 'rematch' is handed to the netplay code
//   (T6StatusTakeRematch) - the restart itself belongs to the match start code.
// T6_STATUS_DUMP=1 (diagnostics): the match info object (sub_8238E3B8) is logged at every mode change, to find the
//   round win counters.

#include "t6_common.h"
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>


void T6StatusMatchInfo(uint32_t* addr);

int T6NetSlowPct();
int T6NetLagPct();    // t6_netplay.cpp: fight frames with a predicted remote input in percent, -1 = not enough yet
int T6NetBehind10();  // t6_netplay.cpp: frames the peer was behind on average x 10, -1 = not enough yet
int T6NetPingAvg();   // t6_netplay.cpp: the ping of those samples in ms, -1 = not enough yet  // t6_netplay.cpp: slow fight frames of this game in percent, -1 = not enough frames yet

namespace {


const char* ModeName(uint32_t m) {
  switch (m) {
    case 0: case 1: case 2: return "intro";
    case 3: return "round";
    case 4: return "fight";
    case 5: return "ko";
    case 6: return "replay";
    case 10: return "between_rounds";
    case 7: case 8: case 9: return "result";
    case 11: case 12: case 25: case 26: case 27: return "menu";
    default: return "other";
  }
}

// Fight player objects (per-frame battle update sub_822D0750 = PS3 FUN_0020fe18: r31 = *(0x828DCE88), r30 = *(0x828DCE8C));
// +112 = rounds won (sub_822CE348: match decided when one reaches the match info's +282, rounds needed)

bool Readable(uint32_t a) {
  MEMORY_BASIC_INFORMATION mbi{};
  return a >= 0x10000 && VirtualQuery(g_t6_membase + a, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
         !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

// Rounds won by player 1 / 2; false outside a battle
bool RoundWins(uint32_t wins[2]) {
  for (int i = 0; i < 2; i++) {
    const uint32_t p = T6PlayerPtr(i);
    if (!Readable(p + 112)) return false;
    wins[i] = T6Rd32(p + 112);
    if (wins[i] > 9) return false;
  }
  return true;
}

struct Status {
  uint32_t mode = 0xFFFFFFFFu;
  uint32_t round_no = 0;
  int winner = -1;  // -1 = match not finished, 0 = draw (equal rounds), 1 / 2 = the player
  bool disconnected = false;
  bool in_battle = false;  // set by 14 -> 0 / 25+ -> 0 (battle loaded), cleared by the exit modes 25+
  uint32_t frames = 0;
  std::atomic<bool> rematch{false};
  std::atomic<bool> park{false};
} g_status;

void WriteStatus() {
  static const char* path = T6Env("T6_NET_STATUS");
  if (!path || !*path) return;
  // Before the battle (boot, menus) the mode is 0 as well: 'loading' until the first 14 -> 0
  std::string out = fmt::format("state {}\nmode {}\nroundno {}\n", !g_status.in_battle && g_status.mode <= 2 ? "loading" : ModeName(g_status.mode),
                                g_status.mode, g_status.round_no);
  if (uint32_t wins[2]; g_status.mode <= 12 && RoundWins(wins)) out += fmt::format("round {} {}\n", wins[0], wins[1]);
  if (g_status.winner >= 0) out += fmt::format("winner {}\n", g_status.winner);
  // frames this game was too slow for 60 per second (the launcher sends it with the result: over 30 % ends the set)
  if (const int slow = T6NetSlowPct(); slow >= 0 && g_status.in_battle) out += fmt::format("slow {}\n", slow);
  // the peer's game lagging behind (the launcher sends it with the result: the server ends the set over its limits)
  if (const int lag = T6NetLagPct(); lag >= 0 && g_status.in_battle) out += fmt::format("lag {} {} {}\n", lag, T6NetBehind10(), T6NetPingAvg());
  if (g_status.disconnected) out += "disconnected\n";
  T6WriteFileAtomic(path, out);  // the launcher never sees half a file
}

void PollCommand() {
  static const char* path = T6Env("T6_NET_CMD");
  if (!path || !*path) return;
  // T6_TEST_PARK_LATE=<ms> (tests): this game takes its 'park' command that much later than it arrived = the peer
  // closes the session first and this side waits for his input (t6_netplay.cpp takes the command in that wait)
  static ULONGLONG park_at = 0;
  if (park_at && GetTickCount64() >= park_at) {
    park_at = 0;
    REXLOG_INFO("T6 status: park command from the launcher (late, test)");
    g_status.park = true;
  }
  FILE* f = T6Open(path, "r");
  if (!f) return;
  char line[64] = {};
  const bool got = std::fgets(line, sizeof(line), f) != nullptr;
  std::fclose(f);
  if (!got) return;
  std::remove(path);
  if (std::strncmp(line, "quit", 4) == 0) {
    REXLOG_INFO("T6 status: quit command from the launcher");
    ExitProcess(0);
  }
  if (const char* late = T6Env("T6_TEST_PARK_LATE"); late && std::strncmp(line, "park", 4) == 0) {
    park_at = GetTickCount64() + std::strtoul(late, nullptr, 0);
    return;
  }
  if (std::strncmp(line, "park", 4) == 0) {
    REXLOG_INFO("T6 status: park command from the launcher");
    g_status.park = true;
  }
  if (std::strncmp(line, "rematch", 7) == 0) {
    REXLOG_INFO("T6 status: rematch command from the launcher");
    g_status.rematch = true;
  }
}

void DumpMatchInfo(uint32_t mode) {
  static const bool on = T6Env("T6_STATUS_DUMP") != nullptr;
  if (!on) return;
  uint32_t g = 0;
  T6StatusMatchInfo(&g);
  if (!g) return;
  std::string words;
  char buf[16];
  for (uint32_t o = 0; o < 0x200; o += 4) {
    std::snprintf(buf, sizeof(buf), " %X", T6Rd32(g + o));
    words += buf;
  }
  REXLOG_INFO("T6 status: mode {} round {} match info {:08X}:{}", mode, g_status.round_no, g, words);
}
}  // namespace

// The match info object (getter sub_8238E3B8, the match setup the battle reads the character codes from)
extern "C" void __imp__sub_8238E3B8(PPCContext& ctx, uint8_t* base);
extern PPCContext* g_main_ctx;  // t6_netplay.cpp: the main thread's own context (set at the first frame boundary)
// A COPY of the context the mode getter last ran with (review 2026-10-18: the pointer itself could be a re-simulated
// frame's temporary context on the stack of RunFrame - read after that frame was gone). The main thread's own context
// is taken when it exists (the callers run on the main thread at the frame boundary).
static PPCContext g_status_ctx_copy;
static bool g_status_ctx_valid = false;
void T6StatusMatchInfo(uint32_t* addr) {
  *addr = 0;
  if (!g_main_ctx && !g_status_ctx_valid) return;
  PPCContext tmp = g_main_ctx ? *g_main_ctx : g_status_ctx_copy;
  __imp__sub_8238E3B8(tmp, g_t6_membase);
  *addr = tmp.r3.u32;
}

// Netplay code: the peer is gone / the launcher asked for a rematch
void T6StatusDisconnected() {
  g_status.disconnected = true;
  WriteStatus();
}
bool T6StatusTakeRematch() { return g_status.rematch.exchange(false); }
// 'park' command: after the match the game goes back to the hold point without a restart (t6_netplay.cpp)
bool T6StatusTakePark() { return g_status.park.exchange(false); }
// the command file is read now (netplay code: the main thread waits for the peer and runs no frame)
void T6StatusPoll() { PollCommand(); }
// The match is over for good (park): nothing of it may show up in the status of the next one
void T6StatusReset() {
  g_status.winner = -1;
  g_status.disconnected = false;
  g_status.round_no = 0;
  g_status.in_battle = false;
}

// Mode getter sub_822CD6F8 (called by the game's mode predicates many times per frame; the commit sub_822CD6E0 has no
// callers left in the recompiled code - it was inlined): the change of the current mode is noticed here
extern "C" void __imp__sub_822CD6F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822CD6F8(PPCContext& ctx, uint8_t* base) {
  __imp__sub_822CD6F8(ctx, base);
  if (!g_t6_membase) return;
  const uint32_t mode = ctx.r3.u32;
  if (mode != g_status.mode) {
    if (!g_main_ctx) {
      g_status_ctx_copy = ctx;
      g_status_ctx_valid = true;
    }
    if (mode == 3) g_status.round_no++;
    if ((mode == 0 || mode == 1) && (g_status.mode == 0xFFFFFFFFu || (g_status.mode >= 7 && g_status.mode != 10))) {
      g_status.round_no = 0;  // a new match
      g_status.winner = -1;
    }
    // End of the match (7 / 8 / 9): the player with more rounds won
    if (uint32_t wins[2]; mode >= 7 && mode <= 9 && g_status.winner < 0 && RoundWins(wins))
      g_status.winner = wins[0] == wins[1] ? 0 : wins[0] > wins[1] ? 1 : 2;
    // battle loaded: 14 -> 0, or a rematch from the result menu (25-27 -> 0)
    if ((g_status.mode == 14 || (g_status.mode >= 25 && g_status.mode <= 28)) && mode == 0) g_status.in_battle = true;
    if (mode >= 25) g_status.in_battle = false;
    g_status.mode = mode;
    WriteStatus();
    DumpMatchInfo(mode);
  }
  static ULONGLONG next_poll = 0;
  if (const ULONGLONG now = GetTickCount64(); now >= next_poll) {
    next_poll = now + 250;
    PollCommand();
  }
}
