// Tekken 6 netplay - frame advantage in the practice (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Frame advantage in the practice (T6_FRAME_DATA=1, launcher; shown by Tekken6App's overlay) ----
// Fight player objects (*(0x828DCE88), *(0x828DCE8C)), found with T6_FP_DUMP 2026-10-13 (jab of user 0 on the standing
// dummy): +0x1C0 move id (0x8001 standing), +0x0C4 frame of the move (1, 2, ...), +0x1E8 the frame of the move from
// which the player can act again (jab 27, the hit reaction to it 25 although its animation runs 30 frames), +0x1AC
// damage of the attack the move is (0 = no attack), +0x228 non-zero while hit (0x10000) / while the attack is active.
// When an attack connects - the defender's move changes into a reaction while the attacker is in an attack - the
// advantage is (defender's frames left) - (attacker's frames left). The jab: (25 - 1) - (27 - 11) = +8, as in the
// frame data lists. Value from player 1's side: + = player 1 acts first.
// When a player can act in a stance move: the move struct (*(player + 0x104), 0x58 bytes, an entry of the
// character's move array) has its cancel list at +0x14: 0x20 bytes per entry - command (64 bit: high word direction
// bits, low word 0x40000000 | buttons 1/2/4/8), requirements pointer (first word 0 = none), extra, input window start /
// end, the frame at which the cancel happens, target move << 16 | option; command 0x8000 = the move's natural end
// (its frame = +0x1E8), last entry. Truth (offline Versus, both players tapping a button, runss_run.ps1 +
// truth_an.py, 2026-10-14): a tapped button came out exactly at the first plain-button cancel frame + 1 of the
// stance - Steve's flicker 525 frame 14 (b+1~B: -1 on block, the frame data list -1; we showed +12), Hwoarang's
// flamingo 571 frame 1 (f+4: +7 on block). Directional cancels are left out (525: d/b+4 at frame 7 is a special
// move, the lists count from 14), and so are button pairs (flicker after FLK 1, 356: 3+4 at frame 1 but 1 / 2 / 3 / 4
// at 18 - the 3+4 showed +16, user: "pochybuju ze to je +16"; the list -1). No such cancel: the natural end, else 1.
// Steve's ducking 676 (1,2,1,2 ~f+3, and other hand-overs the user tried: 769, 662) has only buttons WITH requirements
// and a cancel without command into 572 at frame 22: the old "nothing found -> 1" showed +16. Now such automatic
// cancels are followed (frame F + the target's own frame, up to 4 moves deep); nothing found = -1 (unknown: the
// caller keeps the move's +0x1E8).
// One cancel of a move's list (0x20 bytes at *(move + 0x14)): kind, command, requirements, window, frame, target / options
struct Cancel {
  uint32_t hi, lo, req;
  int w0, w1, at;
  uint32_t x1c;  // target move id << 16 | option bits
  uint32_t Target() const { return x1c >> 16; }
  bool FreeReq() const { return !T6InGameRange(req, 4) || T6Rd32(req) == 0; }  // no requirements at all
  bool PlainButton() const {  // one of 1 / 2 / 3 / 4 pressed, no direction
    const uint32_t btn = lo & 0xFu;
    return hi == 0 && (lo & 0xFFFFFFF0u) == 0x40000000u && (btn == 1 || btn == 2 || btn == 4 || btn == 8);
  }
};
static Cancel ReadCancel(uint32_t c) {
  return {T6Rd32(c), T6Rd32(c + 4), T6Rd32(c + 8), static_cast<int>(T6Rd32(c + 0x10)), static_cast<int>(T6Rd32(c + 0x14)),
          static_cast<int>(T6Rd32(c + 0x18)), T6Rd32(c + 0x1C)};
}
// the game's own requirement check of a cancel (sub_82285FC0(cancel, player, opponent)) as it stands now
static bool CancelAllowed(uint32_t c, uint32_t pl, uint32_t opp) {
  PPCContext tmp = *g_main_ctx;
  tmp.r3.u64 = c;
  tmp.r4.u64 = pl;
  tmp.r5.u64 = opp;
  __imp__sub_82285FC0(tmp, g_t6_membase);
  return tmp.r3.u32 != 0;
}

// fn(address, cancel) for the first 80 cancels of a move up to its end marker (0x8000); fn returns false to stop
template <class Fn>
static void ForEachCancel(uint32_t mv, const Fn& fn) {
  uint32_t c = T6Rd32(mv + 0x14);
  for (int k = 0; k < 80 && T6InGameRange(c, 0x20); k++, c += 0x20) {
    const Cancel x = ReadCancel(c);
    if (!fn(c, x) || x.hi == 0x8000u) break;
  }
}

static int StanceActFrame(uint32_t base, uint32_t mv, int depth = 0) {
  if (!T6InGameRange(mv, 0x58) || depth > 4) return -1;
  int best = 0x7FFFFFFF;
  ForEachCancel(mv, [&](uint32_t, const Cancel& x) {
    const uint32_t target = x.Target();
    if (x.hi == 0x8000u) {
      if (x.at > 0) best = std::min(best, x.at);  // natural end (back to standing)
    } else if (x.PlainButton() && x.FreeReq()) {
      best = std::min(best, std::max(std::max(x.w0, x.at), 1));
    } else if (x.hi == 0 && x.lo == 0 && x.FreeReq() && target < 0x8000u && x.at > 0 && x.at < best && T6InGameRange(base + target * 0x58u, 0x58)) {
      const int e = StanceActFrame(base, base + target * 0x58u, depth + 1);
      if (e > 0) best = std::min(best, x.at + e);
    }
    return true;
  });
  return best != 0x7FFFFFFF ? best : -1;
}

// The connected attack's own unconditional hand-over into a stance (cancel without command and requirements, e.g.
// Hwoarang f+4 502 -> flamingo 571 at frame 28): frames from the move's frame t until the player can act there, or
// -1. Known at the hit, so a stance move pressed at once (f+4 -> RFS 1 straight from the attack, no 571 in between)
// does not hide it (that showed -12 instead of +7). The target's struct: the move array is 0x58 per move id
// (struct of the current move - id * 0x58 = the array).
static int StanceHandOver(uint32_t mv, uint32_t id, uint32_t t) {
  if (!T6InGameRange(mv, 0x58) || id >= 0x8000u) return -1;
  const uint32_t base = mv - id * 0x58u;
  int best = -1;
  ForEachCancel(mv, [&](uint32_t, const Cancel& x) {
    if (x.hi != 0 || x.lo != 0 || !x.FreeReq() || x.Target() >= 0x8000u || x.at < static_cast<int>(t)) return true;
    // the target starts at frame at + 1 of this move; the player can act at its frame StanceActFrame
    if (const int e = StanceActFrame(base, base + x.Target() * 0x58u); e >= 0) {
      const int n = x.at - static_cast<int>(t) + e;
      if (best < 0 || n < best) best = n;
    }
    return true;
  });
  return best;
}

// When can the player act - asked from the game itself (user 2026-10-14: "nemel bys mit akorat nejaky pocitadlo ktery to
// dela samo"; my own reading of the cancel data was wrong twice: 3+4 in Steve's flicker, ducking without plain buttons).
// The game's per-frame cancel processor sub_82286188(list, frame, player, opponent) walks the current move's cancels
// (*(player + 0x104) + 0x14; 0x800B = a group at *(*(player + 0x100) + 0x1A4) + id * 32 up to 0x800C), skips option bit
// 2, takes a cancel when the frame is in its window, the pad matches (sub_822A3D98) and its requirements hold
// (sub_82285FC0), and the move then changes at the cancel's frame. CanAct does the same for "one of 1 / 2 / 3 / 4
// pressed, no direction" without the pad check and without doing it: the earliest frame of the current move (like
// +0x1E8) at which such a press would take effect, the requirements evaluated by the game now; the natural end counts
// too; -1 = none. The player's own strings are cancels like that too, so the caller asks only in moves without damage.
static int CanAct(uint32_t pl, uint32_t opp, uint32_t t) {
  const uint32_t mv = T6Rd32(pl + 0x104), ms = T6Rd32(pl + 0x100);
  if (!T6InGameRange(mv, 0x58) || !g_main_ctx) return -1;
  const uint32_t gb = T6InGameRange(ms, 0x1A8) ? T6Rd32(ms + 0x1A4) : 0;
  int best = 0x7FFFFFFF;
  uint32_t c = T6Rd32(mv + 0x14), ret = 0;
  for (int k = 0; k < 400 && T6InGameRange(c, 0x20); k++, c += 0x20) {
    const Cancel x = ReadCancel(c);
    if (x.hi == 0x8000u) {
      if (x.at > 0) best = std::min(best, std::max(x.at, static_cast<int>(t)));
      break;
    }
    if (x.hi == 0x800Bu) {  // into the group (one level, like the game)
      if (ret == 0 && gb && T6InGameRange(gb + x.Target() * 32u, 0x20)) {
        ret = c;
        c = gb + x.Target() * 32u - 0x20;
      }
      continue;
    }
    if (x.hi == 0x800Cu) {  // end of the group
      if (ret) c = ret, ret = 0;
      continue;
    }
    if (!x.PlainButton() || (x.x1c & 2) || x.w1 < static_cast<int>(t)) continue;
    const int when = std::max(std::max(x.w0, x.at), static_cast<int>(t));
    if (when < best && CancelAllowed(c, pl, opp)) best = when;
  }
  return best != 0x7FFFFFFF ? best : -1;
}

// The attack's own automatic hand-over (a cancel without command whose requirements the game accepts, after the last
// active frame: Hwoarang f+4 502 -> flamingo 571 at 28): the frame, or 0x7FFFFFFF. A next attack started from the
// attack at or after it is a stance move pressed at once (f+4 -> RFS 1 straight from frame 29: that showed -12 instead
// of +7), not a string (Steve b+1 -> its hit variant 640 at 13 is inside the active frames: no).
static int AutoHandOver(uint32_t pl, uint32_t opp) {
  const uint32_t mv = T6Rd32(pl + 0x104);
  if (!T6InGameRange(mv, 0x58) || !g_main_ctx) return 0x7FFFFFFF;
  const int last_active = static_cast<int>(T6Rd32(mv + 0x50));
  int best = 0x7FFFFFFF;
  ForEachCancel(mv, [&](uint32_t c, const Cancel& x) {
    if (x.hi == 0 && x.lo == 0 && x.Target() < 0x8000u && x.at > last_active && x.at < best && CancelAllowed(c, pl, opp)) best = x.at;
    return true;
  });
  return best;
}

std::atomic<int> g_t6_fa_value{0};      // last measured advantage, player 1's side
std::atomic<uint32_t> g_t6_fa_tick{0};  // GetTickCount of that measurement, 0 = none yet
// Startup of player 1's last attack: the frame of the move in which +0x228 turned non-zero (the attack became active;
// jab: frame 10). Shown also when the attack misses.
std::atomic<int> g_t6_fa_startup{0};
std::atomic<uint32_t> g_t6_fa_flag{0};  // player 2's +0x228 at the measurement (0x10000 = hit)
std::atomic<uint32_t> g_t6_fa_startup_tick{0};
void FrameAdvantageTick() {
  static const bool on = T6Env("T6_FRAME_DATA") != nullptr;
  if (!on) return;
  static uint32_t prev_id[2] = {};
  static uint32_t prev_at[2] = {};
  static uint32_t prev_hit[2] = {};
  uint32_t pl[2], id[2], t[2], len[2], dmg[2], hit[2];
  // "Player 1's attack is active": +0x238 == 10 (3 from the start of an attack move, 10 from its active frame - on
  // hit, on block and when it misses; 7 at the start of the second punch of Lee's 1,2 and 10 again from its active
  // frame; 16 / 12 / 0 afterwards). Not +0x228: it stays 0x100 through a whole string. Not +0x1D0 (jab 8, Lee's
  // second punch 10, Jin's kick 19): that is only set when the attack HITS - with it nothing was shown on block.
  static uint32_t prev_active = 0;
  for (int n = 0; n < 2; n++) {
    pl[n] = T6PlayerPtr(n);
    if (!T6PlayerObjectOk(pl[n])) return;
    id[n] = T6Rd32(pl[n] + 0x1C0);
    t[n] = T6Rd32(pl[n] + 0x0C4);
    len[n] = T6Rd32(pl[n] + 0x1E8);
    dmg[n] = T6Rd32(pl[n] + 0x1AC);
    hit[n] = T6Rd32(pl[n] + 0x228);
  }
  // player 1's attack became active: the move struct's active frames (+0x4C first, +0x50 last; Wang's ws4 527 and
  // qcf+2 611 never set +0x238 = 10 - user 2026-10-14: "u wanga to neukazuje ws4 ani qcf2"); +0x238 only without a
  // struct
  uint32_t active = T6Rd32(pl[0] + 0x238) == 10u ? 1u : 0u;
  if (const uint32_t mv = T6Rd32(pl[0] + 0x104); (mv >= 0xA7290000u && mv + 0x58u <= 0xABA90000u) || (mv >= 0x82726000u && mv + 0x58u <= 0x82DB0000u)) {
    const uint32_t a0 = T6Rd32(mv + 0x4C), a1 = T6Rd32(mv + 0x50);
    active = (a0 != 0 && a0 <= a1 && a1 < 1000 && t[0] >= a0 && t[0] <= a1) ? 1u : 0u;
  }
  const bool became_active = prev_at[0] == pl[0] && dmg[0] != 0 && active != 0 && prev_active == 0;
  prev_active = active;
  if (became_active && t[0] < 1000) {
    g_t6_fa_startup.store(static_cast<int>(t[0]));
    g_t6_fa_startup_tick.store(GetTickCount() | 1u);
    REXLOG_INFO("T6 frame data: player 1 move {} active at frame {}", id[0], t[0]);
  }
  // Player 1's attack connected: the dummy's reaction starts (its move changes, or the same reaction starts again:
  // its frame counter goes back) while player 1's move has been active. Only this way round: the dummy does not
  // attack.
  //  - Jin's kicks change into their "it hit" move in the frame the dummy reacts (514 -> 515, damage 0, the frame
  //    counter runs on): the move stays "has been active" until its counter restarts.
  //  - A blocked attack: the dummy is in move 306 for ONE frame, then in the guard reaction proper (jab: 427, 18
  //    frames); the number is taken again when the reaction changes in the next 3 frames.
  //  - Strings (user 2026-10-13: "stringy fungujou spatne"): the next attack of a string can start in the very
  //    frame the dummy reacts - the jab then showed "-14" (the follow-up's whole length against the guard reaction).
  //    So player 1's frames left are those of the move that connected: taken from the frame before when a new move
  //    just started, and frozen (counted down) for the 3 frames in which the reaction may still change.
  //  - Moves into a stance (user 2026-10-14: "frame data jsou spatne u pohybu ze stance nebo ktery jdou do stance",
  //    Steve, Hwoarang): the attack hands over to the stance move (Hwoarang f+4 502 -> flamingo 571 at its frame 29,
  //    a new move without damage, counter from 1) BEFORE its own +0x1E8 (49). f+4 showed -1 instead of +19 RFS on hit.
  //    When the player can act in the stance is the stance move's own data (the stance move's +0x1E8, 45, means
  //    nothing): StanceActFrame. So the number waits until player 1 can act: his frame reaches the connected move's
  //    +0x1E8, or before that his move changes into a move without an attack (not a string's next attack, not Jin's
  //    "it hit" move whose counter runs on) and he reaches its first button frame.
  static bool counted = false;   // this move of player 1 already gave its number
  static bool act = false;       // player 1's current move has had an active frame
  static bool was_act = false;   // ... the move of the frame before
  static uint32_t prev_t0 = 0, prev_t1 = 0;
  static int prev_rem0 = 0, follow = 0;
  static int64_t fr = 0, free0 = -1, free1 = 0;  // frame count; players' "can act" frames of the pending number
  static int64_t last_act = 0;                     // player 1's last active frame of the move (so far)
  fr++;
  const bool new_move = t[0] < prev_t0;
  // The counter: once player 1 is out of the attack in a move without damage (a stance / transition, counter from 1)
  // or back in a neutral state (id >= 0x8000), ask the game every frame when a plain button would take effect
  // (CanAct); the earliest answer lowers the pending "can act" frames (the attack's own +0x1E8 is the start).
  static bool stance = false;   // in a move without damage since the attack (not a string's next hit)
  static int64_t wfree = -1;    // pending whiff: frame player 1 can act
  const bool stance_before = stance;
  if (new_move) stance = dmg[0] == 0;
  int64_t can = -1;
  if (stance || id[0] >= 0x8000u) {
    const int e = id[0] >= 0x8000u ? static_cast<int>(t[0]) : CanAct(pl[0], pl[1], t[0]);
    if (e >= 0) can = fr + e - static_cast<int>(t[0]);
  }
  // he started an attack out of the stance (or out of the attack at / after its automatic hand-over): he could act at
  // the latest the frame before
  static int auto_at = 0x7FFFFFFF;
  if (new_move && dmg[0] != 0 && (stance_before || static_cast<int>(prev_t0) > auto_at)) can = fr - 1;
  if (can >= 0 && free0 > can) free0 = can;
  if (can >= 0 && wfree > can) wfree = can;
  const int64_t stance_free = -1;
  // Whiff (user 2026-10-14: "netrefujes opponenta a ukazujes frame data ... mas ukazovat max na whiff ... - recovery"):
  // an attack move that was active and connected with nothing shows only its recovery - the frames from its last
  // active frame (move struct) until player 1 can act (its +0x1E8, or a stance's first button frame), as -N.
  // A string's next attack started before that: nothing (the string goes on).
  static int64_t wlast = 0;
  static uint32_t wid = 0;
  if (act && !counted && (new_move ? dmg[0] == 0 : (t[0] >= len[0] && prev_t0 < len[0]))) {
    // natural end: now; into a stance: the counter decides (at the latest when he starts something)
    wfree = new_move ? std::min<int64_t>(fr + 1000, can >= 0 ? can : fr + 1000) : fr;
    wlast = last_act;
    wid = new_move ? prev_id[0] : id[0];
    counted = true;
  }
  if (wfree >= 0 && fr >= wfree) {
    const int rec = static_cast<int>(wfree - wlast);
    g_t6_fa_value.store(-rec);
    g_t6_fa_flag.store(0xFFFFFFFFu);
    g_t6_fa_tick.store(GetTickCount() | 1u);
    REXLOG_INFO("T6 frame data: player 1 whiffed with move {} (now move {} frame {}): recovery {}", wid, id[0], t[0], rec);
    wfree = -1;
  }
  if (new_move) {
    act = false;
    counted = false;
  }
  if (active != 0) last_act = fr;
  if (active != 0) act = true;
  const bool attacking = act || (new_move && was_act);
  const int rem0 = (new_move && !act) ? prev_rem0 - 1 : static_cast<int>(len[0]) - static_cast<int>(t[0]);
  const bool reaction = prev_at[1] == pl[1] && id[1] != 0x8001u && (id[1] != prev_id[1] || t[1] < prev_t1) && len[1] < 1000 &&
                        (hit[1] & 0x10000u);  // (really hit / blocked: not the dummy walking or crouching after a whiff)
  static uint32_t flag = 0, conn_id = 0;
  // Knockdown / launch / crumple (user 2026-10-14: qcf+2 showed +9, the list KD): on hit the number waits until the
  // dummy's reaction is over (free1 + 1); is he not standing or crouching then (0x8001 / 0x8002), the overlay shows
  // "KD" (flag 0xFFFFFFFE) - the reaction's +0x1E8 is not when he can act.
  const auto publish = [&] {
    const int adv = static_cast<int>(free1 - free0);
    const int dummy_can = id[1] >= 0x8000u ? static_cast<int>(t[1]) : CanAct(pl[1], pl[0], t[1]);
    // (neutral ids: only standing 0x8001 / crouching 0x8002 count as up - 0x8006 after Wang's qcf+2 is on the ground; a
    //  reaction move past its +0x1E8 the game lets him act from - Hwoarang's 2: 919 - is not a knockdown)
    const bool up = id[1] >= 0x8000u ? (id[1] == 0x8001u || id[1] == 0x8002u) : (dmg[1] != 0 || dummy_can == static_cast<int>(t[1]));  // (attacking = up)
    const bool kd = !(flag & 1) && fr > free1 && !up;
    if (T6Env("T6_FRAME_DATA_DEBUG"))
      REXLOG_INFO("T6 frame data: dummy at publish: move {} t {} len {} can act at {}", id[1], t[1], len[1], dummy_can);
    g_t6_fa_value.store(adv);
    g_t6_fa_flag.store(kd ? 0xFFFFFFFEu : flag);
    g_t6_fa_tick.store(GetTickCount() | 1u);
    REXLOG_INFO("T6 frame data: player 1 {} with move {} (now move {} frame {}): advantage {:+d}{}", (flag & 1) ? "blocked" : "hit",
                conn_id, id[0], t[0], adv, kd ? fmt::format(" -> knockdown (dummy in move {})", id[1]) : std::string());
    free0 = -1;
  };
  if (follow > 0) {
    follow--;
    if (reaction) {
      free1 = fr + static_cast<int>(len[1]) - static_cast<int>(t[1]);
      if (T6Env("T6_FRAME_DATA_DEBUG")) REXLOG_INFO("T6 frame data: reaction again fr {} P2 move {} t {} len {} -> free {}", fr, id[1], t[1], len[1], free1);
    }
    if ((hit[1] & 1) && !(flag & 1)) flag = hit[1];  // (the block bit comes a frame later)
  } else if (!counted && attacking && reaction && len[0] < 1000) {
    // (a reaction credited to the move of the frame before - the next attack of a string has just started - must not
    //  use up the new move's own number: Lee's 1,2 showed the jab's number and never the second punch's)
    if (!(new_move && !act)) counted = true;
    if (free0 >= 0) publish();  // the string's next hit connected before player 1 could act after the last one
    free0 = fr + rem0;
    auto_at = (new_move && !act) ? 0x7FFFFFFF : AutoHandOver(pl[0], pl[1]);
    free1 = fr + static_cast<int>(len[1]) - static_cast<int>(t[1]);
    if (T6Env("T6_FRAME_DATA_DEBUG"))
      REXLOG_INFO("T6 frame data: connect fr {} P1 move {} t {} len {} -> free {} | P2 move {} t {} len {} -> free {}", fr, id[0], t[0], len[0],
                  free0, id[1], t[1], len[1], free1);
    flag = hit[1];
    conn_id = (new_move && !act) ? prev_id[0] : id[0];
    follow = 3;  // (306 = the one frame before the guard reaction: the reaction of the next frames counts)
  }
  if (free0 >= 0 && follow == 0 && fr >= free0 && ((flag & 1) || fr > free1)) publish();
  prev_t0 = t[0];
  prev_t1 = t[1];
  prev_rem0 = static_cast<int>(len[0]) - static_cast<int>(t[0]);
  was_act = act;
  for (int n = 0; n < 2; n++) {
    prev_id[n] = id[n];
    prev_at[n] = pl[n];
    prev_hit[n] = hit[n];
  }
}

// T6_STANCE_AUDIT=1 (tests, 2026-10-14, user: "tohle musis uz hledat sam ... nemuzu tu projizdet vsechny stance"):
// for both players, every hand-over from an attack into a move without damage (a stance) logs StanceActFrame's
// prediction and then how the player left it: into an attack (frame of the stance at which the attack started,
// the pad of the last 3 steps - random play of the Versus harness, T6_VS_RANDOM), or its end. runsudit_an.py
// compares the earliest single-button exit per stance with the prediction.
uint16_t g_audit_in[2][3] = {};
void StanceAudit() {
  static const bool on = T6Env("T6_STANCE_AUDIT") != nullptr;
  if (!on) return;
  struct St { uint32_t pl = 0, id = 0, t = 0, dmg = 0, mv = 0; int64_t root_at = -1; uint32_t root = 0; int e = 0; std::string chain; };
  // the cancel of the left move that leads to the new one (its command tells how the player left)
  const auto via = [&](uint32_t from_mv, uint32_t to) -> std::string {
    if (!T6InGameRange(from_mv, 0x58)) return "?";
    uint32_t c = T6Rd32(from_mv + 0x14);
    for (int k = 0; k < 80 && T6InGameRange(c, 0x20); k++, c += 0x20) {
      const uint32_t hi = T6Rd32(c), req = T6Rd32(c + 8);
      if ((T6Rd32(c + 0x1C) >> 16) == to)
        return fmt::format("cmd {:X}/{:X} req {:X} win {} at {}", hi, T6Rd32(c + 4), T6InGameRange(req, 4) ? T6Rd32(req) : 0, T6Rd32(c + 0x10), T6Rd32(c + 0x18));
      if (hi == 0x8000u) break;
    }
    return "none";
  };
  static St st[2];
  static int64_t fr = 0;
  fr++;
  for (int n = 0; n < 2; n++) {
    const uint32_t pl = T6PlayerPtr(n);
    if (!T6PlayerObjectOk(pl)) continue;
    St& s = st[n];
    const uint32_t id = T6Rd32(pl + 0x1C0), t = T6Rd32(pl + 0x0C4), dmg = T6Rd32(pl + 0x1AC), hitflag = T6Rd32(pl + 0x228), mv = T6Rd32(pl + 0x104);
    const bool fresh = s.pl == pl && (t < s.t || id != s.id);
    if (fresh) {
      if (s.root_at >= 0) {
        const int x = static_cast<int>(fr - s.root_at);
        if (hitflag & 0x10000u) {
          s.root_at = -1;  // hit / blocking: not his choice
        } else if (dmg != 0) {
          REXLOG_INFO("T6 audit P{} root {} E {} exit {} at {} via {} in {:04X} {:04X} {:04X} chain{}", n + 1, s.root, s.e, id, x, via(s.mv, id),
                      g_audit_in[n][0], g_audit_in[n][1], g_audit_in[n][2], s.chain);
          s.root_at = -1;
        } else if (id >= 0x8000u) {
          REXLOG_INFO("T6 audit P{} root {} E {} end {} at {} chain{}", n + 1, s.root, s.e, id, x, s.chain);
          s.root_at = -1;
        } else {
          s.chain += fmt::format(" {}@{}", id, x);
        }
      }
      if (s.root_at < 0 && dmg == 0 && id < 0x8000u && s.dmg != 0 && t <= 2 && !(hitflag & 0x10000u)) {  // (t <= 2: not a hit variant like 640, whose counter runs on)
        s.root_at = fr - (static_cast<int64_t>(t) - 1);  // (x = the frame of the root at which the next move started, minus 1)
        s.root = id;
        s.e = StanceActFrame(mv - id * 0x58u, mv);
        s.chain.clear();
      }
    }
    s.pl = pl;
    s.id = id;
    s.t = t;
    s.dmg = dmg;
    s.mv = mv;
  }
}

