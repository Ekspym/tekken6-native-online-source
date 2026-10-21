// Tekken 6 netplay - practice tools (user 2026-10-19, after Tekken 8 / Street Fighter V): dummy record / playback, block
// after the first hit, position slots. Own menu on BACK (Select), START keeps the game's practice menu:
//   BACK tapped          opens / closes the menu (the whole pad goes to the menu while it is open; while recording a
//                        tap stops the recording); up / down a row, left / right its value, 1 - 4 do it
//   rows                 record the dummy, playback on / off, block after first hit on / off, position slot (Start /
//                        1 / 2 / 3), save the position into the slot, go to the slot's position, close
//   quick (menu closed)  BACK + cross: the round start position, BACK + down: go to the slot's position, BACK + up:
//                        save into the slot, BACK + square / triangle / circle: playback / record / block after hit
//   (physical buttons before the player's layout, PlayStation names: cross = A, circle = B, square = X, triangle = Y;
//   in the menu cross = OK, circle = close)
// (user: "je hrozny problem nastavovat ty pozice protoze pouzivame jen 4 tlacitka ... menu ktery se ovlada ovladacem")
// The dummy is driven through the game's own practice menu option TRAINING DUMMY 1. The menu edits byte 0x827BA853
// (0 STAND, 1 STAND GUARD, 2 STAND TO GUARD, 3 CROUCH, 4 CROUCH GUARD, 5 GUARD ALL, 6 AUTO GUARD, 7 CONTROLLER =
// "control with opponent's controller"; pm5 2026-10-19), the fight uses its copy 0x827C15E7, written when the menu
// closes, in another order: 1 STAND, 2 STAND GUARD, 0 STAND TO GUARD, the others the same (map2). Only the copy is
// changed here (the menu keeps the player's choice): CONTROLLER while recording / playing, GUARD ALL from the
// dummy's first hit until it is free again. The dummy's inputs are its entry of the frame's input (sub_822688E0, 0x4C per player at the input hook: +0 held XInput buttons, +4 / +8 pressed now, +0xC..
// +0x14 the game's button bits, then the sticks; the dummy's own entry stays 0 - its input never comes from a pad,
// not even with CONTROLLER and a second pad, ctl1): recording copies the player's entry to the dummy and clears the
// player's, playback writes the recorded entries. A position slot is a whole game state (the rollback snapshot): both
// players, health, the stage - like Tekken 8's Status Record.

#include "t6_netplay_internal.h"

std::atomic<uint32_t> g_t6_pr_flags{0};  // shown by the overlay: 1 recording, 2 playing, 4 block after hit
std::atomic<uint32_t> g_t6_pr_frames{0};  // frames of the recording (while recording: so far)
std::atomic<uint32_t> g_t6_pr_msg_tick{0};  // GetTickCount of the last message (shown 2 s)
std::mutex g_t6_pr_msg_mutex;
std::string g_t6_pr_msg;
std::vector<std::string> g_t6_pr_lines;  // (under g_t6_pr_msg_mutex) the menu's rows, or the quick help when it is closed
std::atomic<int> g_t6_pr_cursor{-1};      // the selected row; -1 = the menu is closed
// Input display (user 2026-10-20: "chybi nam input display", like Tekken 8): the player's inputs as the game took them,
// one row per change (the newest first) with the frames it was held; directions relative to the facing
std::mutex g_t6_in_mutex;
std::deque<T6InputRow> g_t6_in_rows;       // (under g_t6_in_mutex)
std::atomic<bool> g_t6_in_display{true};  // the menu's "Input display" row
std::atomic<bool> g_t6_fa_display[2]{true, true};  // the menu's "Frame data you / opponent" rows

namespace {
constexpr uint32_t kDummyMenu = 0x827BA853u, kDummyApplied = 0x827C15E7u;
constexpr uint8_t kDummyGuardAll = 5, kDummyController = 7;
constexpr uint32_t kMaxFrames = 30 * 60;
constexpr uint16_t kBack = 0x0020, kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
// the menu and the shortcuts go by the PHYSICAL buttons (before the player's layout), named the PlayStation way the
// user does (2026-10-19: "select plus X" meant cross): cross = A, circle = B, square = X, triangle = Y
constexpr uint16_t kCross = 0x1000, kCircle = 0x2000, kSquare = 0x4000, kTriangle = 0x8000;
uint16_t g_raw_buttons = 0;  // this step's physical buttons of user 0 (+ the keyboard)
constexpr int kStick = 16000;                                                // left stick counts as a direction from here
constexpr uint32_t kInputBytes = 0x40;  // the input part of an entry (the rest belongs to the player object)

using Pad = std::array<uint8_t, 12>;  // XINPUT_GAMEPAD in guest byte order (buttons BE, LT, RT, LX, LY, RX, RY)

enum class Mode { kIdle, kRecord, kPlay };
Mode g_mode = Mode::kIdle;
using Entry = std::array<uint8_t, kInputBytes>;
std::vector<Entry> g_recs[3], g_take;  // the recordings (3 slots, user 2026-10-19) / the one being made
int g_rec_slot = 0;                      // the slot the next recording goes into / the one played
bool g_play_random = false;              // playback: a random recorded slot for every run
int g_playing = 0;                       // the slot of the run playing now
size_t g_play_at = 0;
int g_play_gap = 0;
bool g_block_after_hit = false;
bool g_guarding = false;          // block after hit: GUARD ALL is on
int g_free_frames = 0;
bool g_back_used = false;         // a quick action was used while BACK was held
bool g_menu = false;              // the menu is open
int g_cursor = 0;                 // its selected row (Row)
int g_slot = 0;                   // the position slot of the menu: 0 = the start of the practice, 1 - 3 the player's
enum Row { kRowMoveList, kRowSelf, kRowOpponent, kRowDummy, kRowWakeup, kRowBlock, kRowCounter, kRowPosReset, kRowInputs, kRowFrameData, kRowFrameDataOpp, kRowRecSlot, kRowRecord, kRowPlay, kRowPlayWhich, kRowSlot, kRowSave, kRowLoad, kRowClose, kRowQuit, kRows };
enum Req { kNone, kSave, kLoad };
Req g_req = kNone;
int g_req_slot = 0;
std::unique_ptr<SnapTest> g_slots[4];
bool g_start_taken = false;
int g_join = 0;  // frames until pad 2's START (the dummy went on CONTROLLER)
int g_standing = 0;  // frames both players have stood (the start slot is taken after 30)
uint32_t g_battle_frames = 0;
// A slot holds what the game itself wrote (the session's rule: CalibrateFromMap from the page map, the bytes the
// libraries wrote - sound, Direct3D - keep their live value at a load; all3 2026-10-19: the whole XEX data restored
// put the sound driver's objects back and the next sound call crashed)
void SaveSlot(std::unique_ptr<SnapTest>& slot) {
  CalibrateFromMap(SnapRanges(), g_cal_exclude);
  g_cal_done = true;
  if (!slot) slot = std::make_unique<SnapTest>(false);
  slot->Save();
}

uint16_t Buttons(const Pad& p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
int16_t Axis(const Pad& p, int i) { return static_cast<int16_t>(p[4 + 2 * i] << 8 | p[5 + 2 * i]); }
// back <-> forward of an entry: its directions are the player's own (0x4 back, 0x8 forward; the game's bits 0x40 / 0x80
// in the high half), so the player's "back" (away from the dummy) is the dummy's "forward" (rec4: copied unchanged the
// dummy walked away while it was meant to come closer)
[[maybe_unused]] void SwapBackForward(Entry& e) {  // (kept for a playback after the sides switched - not done yet)
  for (uint32_t k = 0; k < 0x18; k += 4) {
    uint32_t w = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(e.data() + k));
    const uint32_t lo = w & 0x0000000Cu, hi = w & 0x00C00000u;
    w = (w & ~0x00C0000Cu) | ((lo & 4u) << 1) | ((lo & 8u) >> 1) | ((hi & 0x00400000u) << 1) | ((hi & 0x00800000u) >> 1);
    *reinterpret_cast<uint32_t*>(e.data() + k) = _byteswap_ulong(w);
  }
}
bool Neutral(const Entry& e) {
  for (uint32_t k = 0; k < 0x18; k++) if (e[k]) return false;  // (the button words; not checked: whether a stick alone sets them)
  return true;
}
// d-pad bits plus the left stick as directions (for the shortcuts)
uint16_t Dirs(const Pad& p) {
  uint16_t d = Buttons(p) & 0x000F;
  if (Axis(p, 0) < -kStick) d |= kLeft;
  if (Axis(p, 0) > kStick) d |= kRight;
  if (Axis(p, 1) > kStick) d |= kUp;
  if (Axis(p, 1) < -kStick) d |= kDown;
  return d;
}

void Message(std::string m) {
  REXLOG_INFO("T6 practice: {}", m);
  std::lock_guard lock(g_t6_pr_msg_mutex);
  g_t6_pr_msg = std::move(m);
  g_t6_pr_msg_tick.store(GetTickCount() | 1u);
}
void Flags() {
  g_t6_pr_flags.store((g_mode == Mode::kRecord ? 1u : 0u) | (g_mode == Mode::kPlay ? 2u : 0u) | (g_block_after_hit ? 4u : 0u));
}

uint8_t Option() { return g_t6_membase[kDummyApplied]; }
void SetOption(uint8_t v) { g_t6_membase[kDummyApplied] = v; }
// the player's own choice (the menu's byte) as the fight's value
uint8_t UserOption() {
  const uint8_t m = g_t6_membase[kDummyMenu];
  return m == 0 ? 1 : m == 1 ? 2 : m == 2 ? 0 : m <= kDummyController ? m : 1;
}
// TRAINING DUMMY 2 (getting up), the fight's copy 0x827C15EB; its values in the game menu's order (opt3 / opt4
// 2026-10-19: STAND UP was 5, the next ones 1 2 3 4 6 7 8 9 10; the last one, 11, not seen - assumed)
constexpr uint32_t kWakeupApplied = 0x827C15EBu;
constexpr const char* kWakeupNames[11] = {"STAND UP", "QUICK ROLL (front)", "QUICK ROLL (back)", "SPRING FORWARD", "QUICK ROLL BACK",
                                          "SIDE ROLL (front)", "SIDE ROLL (back)", "FORWARD ROLL", "BACKWARD ROLL",
                                          "STAND UP TO MID ATTACK", "STAND UP TO LOW ATTACK"};
constexpr uint8_t kWakeupValues[11] = {5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11};
int Wakeup() {
  const uint8_t v = g_t6_membase[kWakeupApplied];
  for (int k = 0; k < 11; k++)
    if (kWakeupValues[k] == v) return k;
  return 0;
}
void SetWakeup(int k) { g_t6_membase[kWakeupApplied] = kWakeupValues[k]; }
// COUNTER ATTACKS and POSITION RESET of the game's menu, 0 off / 1 on (their bytes went 0 -> 1 when the menu closed with
// them switched on: opt5 2026-10-19). Both bytes are the menu's (ver2: the game's menu shows ON after they were
// written); POSITION RESET works from it (user), COUNTER ATTACKS did not: the fight takes bit 0x10 of 0x827C15C1 (the
// block of the dummy's 0x827C15E7; the only byte of the data / heap that followed ON / OFF / ON exactly, opt6), set with it.
constexpr uint32_t kCounterApplied = 0x827BB033u, kCounterFight = 0x827C15C1u, kPosResetApplied = 0x827BB393u;
// The menu's settings that live in the game's memory (user 2026-10-20: "kdyz udelam position reset tak se hodne
// nastaveni resetuje"): a position slot is a whole game state, so a load put the dummy / getting up / counter hit /
// position reset back to what they were when the slot was saved (the start slot: the defaults). Kept across a load.
struct GameOptions {
  uint8_t dummy_menu, wakeup, counter, counter_bit, pos_reset;
  static GameOptions Now() {
    return {g_t6_membase[kDummyMenu], g_t6_membase[kWakeupApplied], g_t6_membase[kCounterApplied],
            static_cast<uint8_t>(g_t6_membase[kCounterFight] & 0x10), g_t6_membase[kPosResetApplied]};
  }
  void Put() const {
    g_t6_membase[kDummyMenu] = dummy_menu;
    g_t6_membase[kWakeupApplied] = wakeup;
    g_t6_membase[kCounterApplied] = counter;
    g_t6_membase[kCounterFight] = static_cast<uint8_t>((g_t6_membase[kCounterFight] & ~0x10) | counter_bit);
    g_t6_membase[kPosResetApplied] = pos_reset;
  }
};
// the game's own dummy choices (its menu's order, CONTROLLER left out: the recording uses it)
constexpr const char* kDummyNames[7] = {"STAND", "STAND GUARD", "STAND TO GUARD", "CROUCH", "CROUCH GUARD", "GUARD ALL", "AUTO GUARD"};
int MenuDummy() { return std::min<int>(g_t6_membase[kDummyMenu], 6); }
// as if the player chose it in the game's menu: the menu byte and the fight's copy
void SetMenuDummy(int m) {
  g_t6_membase[kDummyMenu] = static_cast<uint8_t>(m);
  if (g_mode == Mode::kIdle && !g_guarding) SetOption(UserOption());
}
// CONTROLLER only works when the game's own menu chooses it: closing the menu with it also sets the dummy's player
// object up for a pad (+0x290 / +0x294, a new input object in the heap - opt7 2026-10-19); the fight's copy written
// alone (7) left the dummy to the game's AI ("zapne se nejakej NPC", ai1 / ai2). So the menu is driven like a player
// would: START, the cursor (0x827BA610, cur1) to TRAINING DUMMY 1 (row 3), left / right until the choice, START.
constexpr uint32_t kGameMenuCursor = 0x827BA610u;
// (14 rows: up from MODE SELECT gave cursor 13 = RETURN TO MAIN MENU, 12 CHARACTER SELECT, 11 CONTROLLER SETUP - opp3)
constexpr int kGameMenuRows = 14, kGameMenuDummyRow = 3, kGameMenuSelectRow = 12;
// (hud1 2026-10-20, a screenshot of the menu: MODE SELECT, RETURN TO GAME PLAY, COMMAND LIST, TRAINING DUMMY 1 / 2,
// COUNTER ATTACKS, COMMAND HISTORY, POSITION RESET, ATTACK DATA, HIT ANALYSIS, CONTROLLER SETUP, CHARACTER SELECT,
// RETURN TO MAIN MENU: 13 rows on screen for 14 cursor values - TRAINING DUMMY 1 = 3 (used since 2026-10-19) puts
// COMMAND LIST at 2; one value between 3 and CHARACTER SELECT 12 has no row on screen)
constexpr int kGameMenuListRow = 2;
// The move list (user 2026-10-20: "chybi nam ... movelist") is the game's own COMMAND LIST: the drive goes there and
// presses cross, then the whole pad (START too) belongs to the game until the fight runs again (GameListTick)
bool g_game_list = false;
int g_list_running = 0;  // frames the fight has run since (the players' move frames moved)
bool g_rebattle = false;  // the drive reached CHARACTER SELECT: the pad hook confirms until the new battle exists
struct Drive {
  int phase = 0;      // 0 off, 1 open, 2 row, 3 value, 4 close, 5 settle
  int last = -1;      // the value (cursor / choice) seen at the last look, and for how many looks it stayed
  int same = 0;
  int steps = 0;      // steps so far (a drive that never gets there gives the pad back after 600 - review)
  bool select = false;  // to CHARACTER SELECT (row 11) and cross there, instead of a TRAINING DUMMY 1 choice
  bool peek = false;    // (research) only open the game's menu and leave it open
  bool list = false;    // to COMMAND LIST and cross there: the move list
  int target = 0;     // the menu choice to reach
  int wait = 0;       // steps until the next press may go
  int hold = 0;       // steps the current press is held
  uint16_t press = 0;
} g_drive;
int g_saved_menu = -1;  // the player's TRAINING DUMMY 1 before we took the dummy
void StartDrive(int target) {
  if (g_t6_membase[kDummyMenu] == target && g_drive.phase == 0) return;
  g_drive = Drive{};
  g_drive.phase = 1;
  g_drive.target = target;
  g_menu = false;
}
bool Driving() { return g_drive.phase != 0 || g_drive.hold > 0; }
void StartListDrive() {
  g_drive = Drive{};
  g_drive.phase = 1;
  g_drive.list = true;
  g_menu = false;
}
// one pad step of the drive: the buttons user 0 presses now (the player's own pad is not used meanwhile)
uint16_t DriveStep() {
  Drive& d = g_drive;
  if (++d.steps > 600) {
    REXLOG_WARN("T6 practice: game menu drive gave up (phase {}, cursor {}, choice {})", d.phase,
                T6Rd32(kGameMenuCursor), g_t6_membase[kDummyMenu]);
    d = Drive{};
    Message("The game's menu did not answer - try again");
    return 0;
  }
  if (d.hold > 0) { d.hold--; return d.press; }
  if (d.wait > 0) { d.wait--; return 0; }
  const auto tap = [&](uint16_t b, int gap) { d.press = b; d.hold = 2; d.wait = gap; return b; };
  switch (d.phase) {
    case 1:
      d.phase = d.peek ? 0 : 2;
      return tap(0x0010, 20);  // START: the game's practice menu
    case 2: {
      // (the cursor word follows a press some steps late: a decision only on a value that stood for 4 looks - the
      // first version pressed one up too many and confirmed CONTROLLER SETUP instead of CHARACTER SELECT, user)
      const int cur = static_cast<int>(T6Rd32(kGameMenuCursor));
      d.same = cur == d.last ? d.same + 1 : 0;
      d.last = cur;
      if (d.same < 4) return 0;
      const int row = d.select ? kGameMenuSelectRow : d.list ? kGameMenuListRow : kGameMenuDummyRow;
      if (cur == row) {
        REXLOG_INFO("T6 practice: game menu cursor on row {}", cur);
        if (d.list) {  // cross on COMMAND LIST; from here the player's pad drives the game's list
          d.phase = 0;
          g_game_list = true;
          g_list_running = 0;
          return tap(kCross, 0);
        }
        if (d.select) {  // cross on CHARACTER SELECT; the rest is the pad hook's confirms
          d.phase = 0;
          g_rebattle = true;
          return tap(kCross, 0);
        }
        d.phase = 3;
        return 0;
      }
      const int down = (row - cur + kGameMenuRows) % kGameMenuRows;
      REXLOG_INFO("T6 practice: game menu cursor {} -> row {}: {}", cur, row, down <= kGameMenuRows / 2 ? "down" : "up");
      d.same = 0;
      d.last = -1;
      return tap(down <= kGameMenuRows / 2 ? kDown : kUp, 5);
    }
    case 3: {
      const int cur = g_t6_membase[kDummyMenu];
      d.same = cur == d.last ? d.same + 1 : 0;
      d.last = cur;
      if (d.same < 4) return 0;
      if (cur == d.target) { d.phase = 4; return 0; }
      const int right = (d.target - cur + 8) % 8;
      d.same = 0;
      d.last = -1;
      return tap(right <= 4 ? kRight : kLeft, 5);
    }
    case 4:
      d.phase = 5;
      return tap(0x0010, 15);  // START: closed, the choice applied
    default:
      d.phase = 0;
      return 0;
  }
}
// the opponent (user: "nejakej character select ... staci opponent"): the launcher's characters (AZAZEL / NANCY left out),
// the P1 costume code (launcher matchCodes: the opponent plays costume 0)
constexpr const char* kOppNames[41] = {"PAUL", "LAW", "LEI", "KING", "YOSHIMITSU", "NINA", "HWOARANG", "XIAOYU", "CHRISTIE",
    "JIN", "JULIA", "KUMA", "PANDA", "BRYAN", "HEIHACHI", "KAZUYA", "LEE", "STEVE", "MARDUK", "MOKUJIN", "JACK-6", "ROGER Jr.",
    "ANNA", "WANG", "GANRYU", "ASUKA", "BRUCE", "BAEK", "DEVIL JIN", "RAVEN", "FENG", "ARMOR KING", "LILI", "DRAGUNOV", "EDDY",
    "BOB", "ZAFINA", "MIGUEL", "LEO", "LARS", "ALISA"};
constexpr int kOppCodes[41] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 21, 23, 24, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47,
    49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 19, 71, 73, 75, 77, 79, 81};  // (odd = P2 costume; KUMA / PANDA one each)
int g_opp = -1, g_self = -1;  // the opponent / the player's own character of the menu (index above)
int OppCode(int k) { return (k == 11 || k == 12) ? kOppCodes[k] : kOppCodes[k] - 1; }
int CharOf(const char* param) {
  const char* e = T6Env(param);
  const int code = e ? T6ToInt(e) : 0;
  for (int k = 0; k < 41; k++)
    if (kOppCodes[k] == code || OppCode(k) == code) return k;
  return 16;  // (LEE)
}
int CurrentOpp() { return g_opp >= 0 ? g_opp : (g_opp = CharOf("T6_CHAR_P2")); }
int CurrentSelf() { return g_self >= 0 ? g_self : (g_self = CharOf("T6_CHAR_P1")); }
void ClearForNewBattle();
void ChangeOpponent() {
  // the player's own character too (user: "chci aby se dal zmenit i tvuj character"); a new own character loses the
  // launcher's custom look (it belongs to the old one), the same one keeps it
  if (CharOf("T6_CHAR_P1") != CurrentSelf()) {
    T6SetParam("T6_CHAR_P1", std::to_string(kOppCodes[CurrentSelf()]));
    T6SetParam("T6_CUSTOM_P1", "");
  }
  T6SetParam("T6_CHAR_P2", std::to_string(OppCode(CurrentOpp())));
  ClearForNewBattle();
  g_drive = Drive{};
  g_drive.phase = 1;
  g_drive.select = true;
  g_menu = false;
  Message(fmt::format("Loading {} vs {}...", kOppNames[CurrentSelf()], kOppNames[CurrentOpp()]));
}
void TakeDummy() {
  g_guarding = false;
  if (g_t6_membase[kDummyMenu] != kDummyController) {
    g_saved_menu = g_t6_membase[kDummyMenu];
    StartDrive(kDummyController);
  }
}
void ReleaseDummy() {
  const int back = g_saved_menu >= 0 ? g_saved_menu : 0;
  g_saved_menu = -1;
  StartDrive(back);
}
// another opponent: the recordings and the position slots belong to the old one
void ClearForNewBattle() {
  for (auto& r : g_recs) r.clear();
  for (auto& sl : g_slots) sl.reset();
  g_start_taken = false;
  g_standing = 0;
  g_guarding = false;
  g_free_frames = 0;
  g_req = kNone;
  g_slot = 0;
}

void StopRecording() {
  // the recording runs from the first input to the last one (the waits before / after are no part of it)
  size_t a = 0, b = g_take.size();
  while (a < b && Neutral(g_take[a])) a++;
  while (b > a && Neutral(g_take[b - 1])) b--;
  g_mode = Mode::kIdle;
  ReleaseDummy();
  if (b > a) {
    auto& rec = g_recs[g_rec_slot];
    rec.assign(g_take.begin() + static_cast<std::ptrdiff_t>(a), g_take.begin() + static_cast<std::ptrdiff_t>(b));
    g_t6_pr_frames.store(static_cast<uint32_t>(rec.size()));
    Message(fmt::format("Recording {} saved ({:.1f} s)", g_rec_slot + 1, rec.size() / 60.0));
  } else {
    Message("Nothing recorded");
  }
  g_take.clear();
}

std::string SlotName(int s) { return s == 0 ? std::string("Start") : fmt::format("slot {}", s); }

// the slot of the next run: the chosen one, or (random) one of the recorded ones; false = nothing recorded
bool NextRun() {
  int have[3], n = 0;
  for (int k = 0; k < 3; k++)
    if (!g_recs[k].empty()) have[n++] = k;
  if (n == 0) return false;
  if (g_play_random) {
    static uint32_t rng = GetTickCount() | 1u;
    rng = rng * 1664525u + 1013904223u;
    g_playing = have[(rng >> 8) % n];
  } else {
    if (g_recs[g_rec_slot].empty()) return false;
    g_playing = g_rec_slot;
  }
  g_t6_pr_frames.store(static_cast<uint32_t>(g_recs[g_playing].size()));
  return true;
}
void ToggleRecord() {
  if (g_mode == Mode::kRecord) {
    StopRecording();
  } else {
    if (g_mode == Mode::kPlay) ReleaseDummy();
    g_mode = Mode::kRecord;
    g_take.clear();
    TakeDummy();
    Message("Recording - you control the dummy (START or SELECT stops)");
  }
}
void TogglePlayback() {
  if (g_mode == Mode::kPlay) {
    g_mode = Mode::kIdle;
    ReleaseDummy();
    Message("Playback off");
  } else if (g_mode == Mode::kRecord) {
    Message("Stop the recording first (START)");
  } else if (!NextRun()) {
    Message(g_play_random ? "Nothing recorded yet - record the dummy first" : fmt::format("Recording {} is empty - record it first", g_rec_slot + 1));
  } else {
    g_mode = Mode::kPlay;
    g_play_at = 0;
    g_play_gap = 0;
    TakeDummy();
    Message("Playback on");
  }
}
void ToggleBlock() {
  g_block_after_hit = !g_block_after_hit;
  if (!g_block_after_hit && g_guarding) {
    g_guarding = false;
    if (g_mode == Mode::kIdle) SetOption(UserOption());
  }
  Message(g_block_after_hit ? "Block after first hit: ON" : "Block after first hit: OFF");
}
void SaveToSlot() {
  if (g_slot == 0) g_slot = 1;  // (the start stays the start: the first player slot instead)
  g_req = kSave;
  g_req_slot = g_slot;
}
void LoadSlot() {
  g_req = kLoad;
  g_req_slot = g_slot;
}

// the overlay's lines: the menu, or the quick help while it is closed
void PublishMenu() {
  std::vector<std::string> lines;
  const char* on = "ON";
  const char* off = "OFF";
  if (g_menu) {
    const auto recName = [](int k) { return fmt::format("{} ({})", k + 1, g_recs[k].empty() ? "empty" : fmt::format("{:.1f} s", g_recs[k].size() / 60.0)); };
    lines.push_back("Move list");
    lines.push_back(fmt::format("You               < {} >   (cross: load)", kOppNames[CurrentSelf()]));
    lines.push_back(fmt::format("Opponent          < {} >   (cross: load)", kOppNames[CurrentOpp()]));
    lines.push_back(fmt::format("Dummy             < {} >", kDummyNames[MenuDummy()]));
    lines.push_back(fmt::format("Getting up        < {} >", kWakeupNames[Wakeup()]));
    lines.push_back(fmt::format("Block after first hit  < {} >", g_block_after_hit ? on : off));
    lines.push_back(fmt::format("Counter hit       < {} >", g_t6_membase[kCounterApplied] ? on : off));
    lines.push_back(fmt::format("Position reset    < {} >", g_t6_membase[kPosResetApplied] ? on : off));
    lines.push_back(fmt::format("Input display     < {} >", g_t6_in_display.load() ? on : off));
    lines.push_back(fmt::format("Frame data: you      < {} >", g_t6_fa_display[0].load() ? on : off));
    lines.push_back(fmt::format("Frame data: opponent < {} >", g_t6_fa_display[1].load() ? on : off));
    lines.push_back(fmt::format("Recording slot    < {} >", recName(g_rec_slot)));
    lines.push_back(g_mode == Mode::kRecord ? fmt::format("Record into slot {}  (recording...)", g_rec_slot + 1) : fmt::format("Record into slot {}", g_rec_slot + 1));
    lines.push_back(fmt::format("Playback          < {} >", g_mode == Mode::kPlay ? on : off));
    lines.push_back(fmt::format("Play              < {} >", g_play_random ? std::string("RANDOM") : fmt::format("slot {}", g_rec_slot + 1)));
    lines.push_back(fmt::format("Position slot     < {} >", SlotName(g_slot)));
    lines.push_back(fmt::format("Save position to {}", SlotName(g_slot == 0 ? 1 : g_slot)));
    lines.push_back(fmt::format("Go to position: {}{}", SlotName(g_slot), g_slots[g_slot] ? "" : " (empty)"));
    lines.push_back("Close menu");
    lines.push_back("Close practice");
    lines.push_back("");
    lines.push_back("Cross: OK   Circle / START: close");
    lines.push_back("Quick, menu closed (SELECT +):");
    lines.push_back("  cross: round start    down / up: go to / save position");
    lines.push_back("  square: playback    triangle: record    circle: block after hit");
  }  // (closed: no help on screen - user 2026-10-20: "lidi vi ze maji zmacknout start")
  // (only on a change: the overlay holds the mutex while it draws - review)
  static std::vector<std::string> last;
  g_t6_pr_cursor.store(g_menu ? g_cursor : -1);
  if (lines == last) return;
  last = lines;
  std::lock_guard lock(g_t6_pr_msg_mutex);
  g_t6_pr_lines = std::move(lines);
}

// the menu open: up / down a row, left / right its value, 1 - 4 do it
void MenuInput(uint16_t pressed, uint16_t dirs_pressed) {
  if (dirs_pressed & kUp) g_cursor = (g_cursor + kRows - 1) % kRows;
  if (dirs_pressed & kDown) g_cursor = (g_cursor + 1) % kRows;
  const int lr = (dirs_pressed & kLeft) ? -1 : (dirs_pressed & kRight) ? 1 : 0;
  if (pressed & kCircle) {  // (user: "kolecko nema prepinat menu ale ma me dostat z toho menu")
    g_menu = false;
    return;
  }
  const bool act = (pressed & kCross) != 0;
  if (!lr && !act) return;
  switch (g_cursor) {
    case kRowMoveList:
      if (act && g_mode != Mode::kIdle) Message("Stop the recording / playback first");
      else if (act && !g_start_taken) Message("The move list opens once the fight runs");  // (the intro: no game menu - list2)
      else if (act) StartListDrive();
      break;
    case kRowSelf:
      if (lr) g_self = (CurrentSelf() + lr + 41) % 41;
      if (act && g_mode != Mode::kIdle) Message("Stop the recording / playback first");
      else if (act) ChangeOpponent();
      break;
    case kRowOpponent:
      if (lr) g_opp = (CurrentOpp() + lr + 41) % 41;
      if (act && g_mode != Mode::kIdle) Message("Stop the recording / playback first");
      else if (act) ChangeOpponent();
      break;
    case kRowDummy:
      SetMenuDummy((MenuDummy() + (lr ? lr : 1) + 7) % 7);
      break;
    case kRowWakeup:
      SetWakeup((Wakeup() + (lr ? lr : 1) + 11) % 11);
      break;
    case kRowCounter:
      g_t6_membase[kCounterApplied] = g_t6_membase[kCounterApplied] ? 0 : 1;
      g_t6_membase[kCounterFight] = static_cast<uint8_t>((g_t6_membase[kCounterFight] & ~0x10) | (g_t6_membase[kCounterApplied] ? 0x10 : 0));
      break;
    case kRowPosReset:
      g_t6_membase[kPosResetApplied] = g_t6_membase[kPosResetApplied] ? 0 : 1;
      break;
    case kRowInputs:
      g_t6_in_display.store(!g_t6_in_display.load());
      break;
    case kRowFrameData:
    case kRowFrameDataOpp: {
      auto& on_off = g_t6_fa_display[g_cursor == kRowFrameDataOpp ? 1 : 0];
      on_off.store(!on_off.load());
      break;
    }
    case kRowRecSlot:
      if (g_mode != Mode::kRecord) g_rec_slot = (g_rec_slot + (lr ? lr : 1) + 3) % 3;
      break;
    case kRowPlayWhich:
      g_play_random = !g_play_random;
      break;
    case kRowRecord:
      if (act) {
        g_menu = false;  // (the recording runs with the menu closed)
        ToggleRecord();
      }
      break;
    case kRowPlay:
      if (act || (lr > 0) != (g_mode == Mode::kPlay)) TogglePlayback();
      break;
    case kRowBlock:
      if (act || (lr > 0) != g_block_after_hit) ToggleBlock();
      break;
    case kRowSlot:
      g_slot = (g_slot + (lr ? lr : 1) + 4) % 4;
      break;
    case kRowSave:
      if (act) SaveToSlot();
      break;
    case kRowLoad:
      if (act) {
        LoadSlot();
        g_menu = false;
      }
      break;
    case kRowClose:
      if (act) g_menu = false;
      break;
    case kRowQuit:
      if (act) {  // (user: "aby menu melo close practice"; the launcher sees the practice end like before)
        REXLOG_INFO("T6 practice: closed from the practice menu");
        ExitProcess(0);
      }
      break;
  }
}
}  // namespace

// User 0's pad of a practice step (after the button layout and the keyboard): BACK and the practice menu. BACK never
// reaches the game; while the menu is open (and while BACK is held) nothing of the pad does.
void PracticePadUser0(uint8_t* g, bool connected) {
  if (!g_practice_battle) return;
  Pad p{};
  if (connected) std::memcpy(p.data(), g, 12);
  // the menu's buttons: the physical pad + the keyboard (not the player's layout: one press = one action)
  const uint16_t b = connected ? g_raw_buttons : 0;
  g_raw_buttons = 0;
  static uint16_t prev_dirs = 0, prev_b = 0;
  // what was held when the menu / a quick action let go of the pad stays away from the game until it is let go
  // (user 2026-10-19: OK on "Go to position" with X also punched)
  static uint16_t swallow = 0;
  static bool swallow_stick = false, prev_eat = false;
  const uint16_t dirs = Dirs(p);
  if (Driving()) {
    // the drive presses for the player; what he holds meanwhile counts from now on (no false press after it - review)
    const uint16_t d = DriveStep();
    std::memset(g, 0, 12);
    g[0] = static_cast<uint8_t>(d >> 8);
    g[1] = static_cast<uint8_t>(d);
    prev_b = b;
    prev_dirs = dirs;
    prev_eat = true;
    PublishMenu();
    return;
  }
  if (g_game_list) {
    // the game's COMMAND LIST (and its menu behind it) is open: the pad as the player holds it, START included -
    // closing the list goes back to the game's menu, START / RETURN TO GAME PLAY to the fight
    prev_b = b;
    prev_dirs = dirs;
    prev_eat = true;
    PublishMenu();
    return;
  }
  const uint16_t pressed = static_cast<uint16_t>(b & ~prev_b), dirs_pressed = static_cast<uint16_t>(dirs & ~prev_dirs);
  bool eat = g_menu;
  if ((pressed & 0x0010) && !(b & kBack)) {
    // START opens / closes our menu too (user: "start otviral nase menu a ne menu hry"); while recording it stops
    if (g_mode == Mode::kRecord) StopRecording();
    else g_menu = !g_menu;
    eat = true;
  } else if (b & 0x0010) {
    eat = true;  // (held START: nothing of it reaches the game)
  } else if (b & kBack) {
    eat = true;
    if (!g_menu) {
      // quick actions with BACK held
      if (dirs_pressed & kDown) { g_back_used = true; LoadSlot(); }
      if (dirs_pressed & kUp) { g_back_used = true; SaveToSlot(); }
      if (pressed & kCross) {  // (user: "select plus x reset na basic pozici round start pozici" - cross)
        g_back_used = true;
        g_req = kLoad;
        g_req_slot = 0;
      }
      if (pressed & kTriangle) { g_back_used = true; ToggleRecord(); }
      if (pressed & kSquare) { g_back_used = true; TogglePlayback(); }
      if (pressed & kCircle) { g_back_used = true; ToggleBlock(); }
    }
  } else if (prev_b & kBack) {
    // BACK tapped (no quick action): stops a recording, nothing else - the menu is on START only (user: "select at
    // neotvira menu ... ted nam to dela start")
    if (!g_back_used && g_mode == Mode::kRecord) StopRecording();
    g_back_used = false;
    eat = true;
  } else if (g_menu) {
    MenuInput(pressed, dirs_pressed);
  }
  prev_b = b;
  prev_dirs = dirs;
  PublishMenu();
  const uint16_t pb = Buttons(p);  // (the pad as the game gets it: the player's layout)
  if (prev_eat && !eat) {
    swallow = static_cast<uint16_t>(pb & ~kBack);
    swallow_stick = (dirs & ~(pb & 0x000F)) != 0;  // (a direction from the stick, not the d-pad)
  }
  prev_eat = eat;
  swallow &= pb;  // (a let go button / direction is free again)
  if (swallow_stick && std::abs(Axis(p, 0)) < 7849 && std::abs(Axis(p, 1)) < 7849) swallow_stick = false;
  if (eat) {
    std::memset(p.data(), 0, 12);
  } else {
    // (BACK never reaches the game, START neither: the game's own practice menu is closed to the player - user: "mozna
    // nepovolat lidem otevrit klasicke menu hry"; our menu drives it itself when it has to)
    const uint16_t keep = static_cast<uint16_t>(pb & ~swallow & ~kBack & ~0x0010);
    p[0] = static_cast<uint8_t>(keep >> 8);
    p[1] = static_cast<uint8_t>(keep);
    if (swallow_stick) std::memset(p.data() + 4, 0, 4);  // (left stick x / y)
  }
  if (connected) std::memcpy(g, p.data(), 12);
}

// The game's list / menu closed = the fight runs again: a player's move frame (+0x0C4) moved in 10 frames in a row
// (the game's pause stops both; standing counts up). Then START is our menu's again.
// (research) T6_PRACTICE_MOVE_LIST=<frame>: the move list opened at that battle frame (as the menu row does)
static void GameListTick() {
  if (static const int at = T6EnvInt("T6_PRACTICE_MOVE_LIST", 0);
      at > 0 && g_battle_frames == static_cast<uint32_t>(at))
    StartListDrive();
  if (!g_game_list) return;
  static uint32_t last[2] = {};
  bool moved = false;
  for (int n = 0; n < 2; n++) {
    const uint32_t pl = T6PlayerPtr(n);
    const uint32_t t = T6PlayerObjectOk(pl) ? T6Rd32(pl + 0x0C4) : 0;
    moved |= t != last[n];
    last[n] = t;
  }
  g_list_running = moved ? g_list_running + 1 : 0;
  if (g_list_running >= 10) {
    g_game_list = false;
    REXLOG_INFO("T6 practice: the game's command list / menu closed (frame {})", g_battle_frames);
  }
}

// Every practice frame (frame boundary): block after the first hit, the position slots
void PracticeTick() {
  if (!g_practice_battle) return;
  g_battle_frames++;
  // (research) T6_PRACTICE_FORCE=<hex address>=<byte>,..: written once 300 frames into the battle
  if (const char* f = T6Env("T6_PRACTICE_FORCE"); f && g_battle_frames == 300)
    T6ForEachPair(f, '=', [](const std::string& a, const std::string& v) {
      g_t6_membase[std::strtoul(a.c_str(), nullptr, 16)] = static_cast<uint8_t>(std::strtoul(v.c_str(), nullptr, 0));
      REXLOG_INFO("T6 practice: forced {} = {}", a, v);
    });
  T6CrowdPagesFrame();
  GameListTick();
  // (research) T6_PRACTICE_GAME_MENU=<frame>: the game's own practice menu opened at that battle frame and left open
  // (which rows it has: COMMAND LIST for the move list?)
  if (static const int peek_at = T6EnvInt("T6_PRACTICE_GAME_MENU", 0);
      peek_at > 0 && g_battle_frames == static_cast<uint32_t>(peek_at)) {
    g_drive = Drive{};
    g_drive.phase = 1;
    g_drive.peek = true;
    REXLOG_INFO("T6 practice: game menu opened for a look at frame {}", g_battle_frames);
  }
  if (static const bool mlog = T6Env("T6_PRACTICE_MOVE_LOG") != nullptr; mlog) {
    static uint32_t last[2] = {};
    for (int n = 0; n < 2; n++) {
      const uint32_t pl = T6PlayerPtr(n);
      if (!T6PlayerObjectOk(pl)) continue;
      if (const uint32_t id = T6Rd32(pl + 0x1C0); id != last[n]) {
        REXLOG_INFO("T6 practice move: frame {} P{} move {} (mode {})", g_battle_frames, n + 1, id, static_cast<int>(g_mode));
        last[n] = id;
      }
    }
  }
  // (research) T6_CROWD_WATCH=1: frames 300..360, the words of the first crowd object that change every frame
  if (T6Env("T6_CROWD_WATCH") && g_battle_frames >= 300 && g_battle_frames < 330) {
    static uint32_t n = 0;
    static std::vector<uint32_t> prev;
    if (!n) {
      const auto nodes = T6CrowdNodes();
      if (!nodes.empty()) n = nodes[nodes.size() / 2];
    }
    if (n) {
      std::vector<uint32_t> now(0x800);
      for (uint32_t k = 0; k < 0x800; k++) now[k] = T6Rd32(n + 4 * k);
      if (!prev.empty()) {
        std::string w;
        for (uint32_t k = 0; k < 0x800; k++)
          if (now[k] != prev[k]) w += fmt::format(" +{:X}={:08X}", 4 * k, now[k]);
        REXLOG_INFO("T6 crowd watch {:08X} frame {}:{}", n, g_battle_frames, w.substr(0, 1500));
      }
      prev = std::move(now);
    }
  }
  if (T6Env("T6_CROWD_PTRS") && g_battle_frames == 300) {  // (research: pointer words in the crowd objects' bodies)
    std::map<uint32_t, int> offs;
    const auto nodes = T6CrowdNodes();
    for (const uint32_t n : nodes)
      for (uint32_t o = 0; o < 0x2000; o += 4) {
        const uint32_t v = T6Rd32(n + o);
        if ((v >= 0x82000000u && v < 0x82DB0000u) || (v >= 0xA7290000u && v < 0xABC00000u)) offs[o]++;
      }
    std::string w;
    for (const auto& [o, c] : offs)
      if (c >= 5) w += fmt::format(" +{:X}:{}", o, c);
    REXLOG_INFO("T6 crowd ptrs ({} objects):{}", nodes.size(), w);
  }
  if (const char* cd = T6Env("T6_CROWD_DUMP"); cd && g_battle_frames == 300) {  // (research: the crowd's memory)
    const auto nodes = T6CrowdNodes();
    REXLOG_INFO("T6 crowd: {} nodes", nodes.size());
    for (size_t k = 0; k < nodes.size(); k++) {
      const uint32_t n = nodes[k];
      std::string w;
      for (uint32_t o = 0; o < 0x40; o += 4) w += fmt::format(" {:08X}", T6Rd32(n - 0x10 + o));
      REXLOG_INFO("T6 crowd: {:08X} vt {:08X} gap {:X} |{}", n, T6Rd32(n), k ? n - nodes[k - 1] : 0, w);
    }
  }
  // (research) T6_PRACTICE_OPT_LOG=1: every change of the practice menu bytes 0x827BA840.. and their fight copies
  // 0x827C15C0.. (which byte is which option)
  if (T6Env("T6_PRACTICE_OPT_LOG")) {
    static uint8_t last[2][0x400];
    static bool primed = false;
    const uint32_t bases[2] = {0x827BA700u, 0x827C1400u};
    for (int r = 0; r < 2; r++)
      for (uint32_t k = 0; k < 0x400; k++) {
        const uint8_t v = g_t6_membase[bases[r] + k];
        if (primed && v != last[r][k])
          REXLOG_INFO("T6 practice opt: frame {} {:08X} {} -> {}", g_battle_frames, bases[r] + k, last[r][k], v);
        last[r][k] = v;
      }
    primed = true;
  }
  // the fight's counter bit follows the menu's COUNTER ATTACKS every frame (user: "counter hit funguje jen obcas" - the
  // game clears / sets it by itself at times; the menu byte is the setting)
  if (const uint8_t c = g_t6_membase[kCounterFight], want = g_t6_membase[kCounterApplied] ? 0x10 : 0; (c & 0x10) != want) {
    g_t6_membase[kCounterFight] = static_cast<uint8_t>((c & ~0x10) | want);
    static int logged = 0;
    if (logged++ < 20) REXLOG_INFO("T6 practice: counter bit put back to {} (frame {})", want ? "on" : "off", g_battle_frames);
  }
  if (static int last = -1; Option() != last) {
    REXLOG_INFO("T6 practice: dummy {} -> {} (frame {})", last, Option(), g_battle_frames);
    last = Option();
  }
  // the start of the practice is slot 0 ("Start"): after the intro, once both players have stood (move 0x8001) for 30
  // frames at their start positions (sl2: a slot from inside the intro crashed when it was loaded during the fight)
  // the dummy on CONTROLLER (recording / playback): the game shows "Press START button of controller in use" until pad 2
  // presses START (user: no such text during a recording) - its neutral pad presses it once, shortly after the drive
  if (static bool was = false; (g_t6_membase[kDummyMenu] == kDummyController && !Driving()) != was) {
    was = !was;
    if (was) g_join = 12;
  }
  if (g_join > 0) g_join--;
  // (test) T6_PRACTICE_AUTO_RECORD=<frame>: the recording starts by itself at that battle frame
  if (static const int auto_rec = T6EnvInt("T6_PRACTICE_AUTO_RECORD", 0); auto_rec && g_battle_frames == static_cast<uint32_t>(auto_rec))
    ToggleRecord();
  if (!g_start_taken) {
    const uint32_t a = T6PlayerPtr(0), b = T6PlayerPtr(1);
    const bool both = T6PlayerObjectOk(a) && T6PlayerObjectOk(b) && T6Rd32(a + 0x1C0) == 0x8001u && T6Rd32(b + 0x1C0) == 0x8001u;
    g_standing = both ? g_standing + 1 : 0;
  }
  if (!g_start_taken && g_standing >= 30 && !Driving() && !g_rebattle) {
    g_start_taken = true;
    SaveSlot(g_slots[0]);
    REXLOG_INFO("T6 practice: start position saved ({} MB)", g_slots[0]->bytes >> 20);
  }
  if (g_req != kNone && !Driving()) {  // (a load / save waits for the menu drive - review)
    const Req r = g_req;
    g_req = kNone;
    const int s = g_req_slot;
    if (r == kSave && g_mode != Mode::kIdle) {
      Message("Stop the recording / playback first, then save the position");
    } else if (r == kSave) {
      // the slot keeps the player's own dummy option, never ours
      const uint8_t ours = Option();
      SetOption(UserOption());
      SaveSlot(g_slots[s]);
      SetOption(ours);
      Message(fmt::format("Position saved ({})", SlotName(s)));
    } else if (!g_slots[s]) {
      Message(fmt::format("Nothing saved in {} yet - menu: Save position", SlotName(s)));
    } else {
      const GameOptions options = GameOptions::Now();
      T6VoicesBeforeLoad();
      const bool ok = g_slots[s]->Load();
      T6VoicesAfterLoad();
      options.Put();  // (before the dummy's option below is taken from the menu byte)
      g_guarding = false;
      g_free_frames = 0;
      // (a slot is saved without our CONTROLLER: during a playback the menu is driven to it again)
      if (g_mode != Mode::kIdle) {
        g_saved_menu = -1;
        g_take.clear();  // (a recording starts again from the loaded position - review)
        TakeDummy();
      } else {
        SetOption(UserOption());
      }
      g_play_at = 0;
      g_play_gap = 0;
      Message(ok ? fmt::format("Reset ({})", SlotName(s)) : "Reset failed");
    }
  }
  Flags();
  if (!g_block_after_hit || g_mode != Mode::kIdle) return;
  const uint32_t dummy = T6PlayerPtr(1);
  if (!T6PlayerObjectOk(dummy)) return;
  const uint32_t hit = T6Rd32(dummy + 0x228);  // 0x10000 hit, 0x10001 blocked
  if (!g_guarding) {
    if (hit == 0x10000u) {
      g_guarding = true;
      g_free_frames = 0;
      SetOption(kDummyGuardAll);
    }
    return;
  }
  // free again: no hit / guard reaction and standing (0x8001) or a move that lets it act, for 20 frames in a row -> the
  // player's own option (all3: standing alone runs 1..53 of 53, "can act" was hardly ever seen)
  const uint32_t t = T6Rd32(dummy + 0x0C4), len = T6Rd32(dummy + 0x1E8), id = T6Rd32(dummy + 0x1C0);
  if (static const bool mlog = T6Env("T6_PRACTICE_MOVE_LOG") != nullptr; mlog && g_battle_frames % 15 == 0)
    REXLOG_INFO("T6 practice guard: frame {} hit {:X} t {} len {} free {}", g_battle_frames, hit, t, len, g_free_frames);
  g_free_frames = (hit == 0 && (id == 0x8001u || t >= len)) ? g_free_frames + 1 : 0;
  if (g_free_frames >= 20) {
    g_guarding = false;
    SetOption(UserOption());
  }
}

void PracticeRawPad(uint16_t buttons) { g_raw_buttons = buttons; }

bool PracticeDummyStart() { return g_join > 0 && g_join <= 4; }
bool PracticeDummyPad() { return g_practice_battle && g_mode != Mode::kIdle; }

// The players' inputs of a frame as the fight takes them (sub_822688E0: one 0x4C entry per player at the input hook)
// The input display's rows from the player's entry of this frame: word +0xC holds the game's bits - directions of the
// player's own facing (0x00100000 up, 0x00200000 down, 0x00400000 back, 0x00800000 forward) and the buttons 1
// 0x04000000, 2 0x01000000, 3 0x00040000, 4 0x00020000 (ent_a 2026-10-20: T6_PRACTICE_SEQ taps + T6_PRACTICE_ENTRY_LOG)
static void NoteInputRow(const uint8_t* p1) {
  const uint32_t w = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p1 + 0xC));
  T6InputRow r;
  r.dirs = static_cast<uint8_t>((w >> 20) & 0xF);
  r.buttons = static_cast<uint8_t>(((w & 0x04000000u) ? 1 : 0) | ((w & 0x01000000u) ? 2 : 0) | ((w & 0x00040000u) ? 4 : 0) |
                                   ((w & 0x00020000u) ? 8 : 0));
  std::lock_guard lock(g_t6_in_mutex);
  if (!g_t6_in_rows.empty() && g_t6_in_rows.front().dirs == r.dirs && g_t6_in_rows.front().buttons == r.buttons) {
    if (g_t6_in_rows.front().frames < 999) g_t6_in_rows.front().frames++;
    return;
  }
  g_t6_in_rows.push_front(r);
  if (g_t6_in_rows.size() > kInputRows) g_t6_in_rows.pop_back();
}

void PracticeEntries(uint8_t* p1, uint8_t* p2) {
  if (!g_practice_battle) return;
  if (Driving()) return;  // (the game's menu is being driven: the fight stands)
  if (!g_game_list) NoteInputRow(p1);  // (before a recording clears the player's entry: the player's own inputs drive the dummy then)
  if (g_mode == Mode::kRecord) {
    // the player's input drives the dummy, the player's own character stands
    Entry e;
    std::memcpy(e.data(), p1, kInputBytes);
    // (no back <-> forward swap: with CONTROLLER chosen through the game's menu the dummy takes the pad's directions
    // as the screen shows them - the swap was for the AI mode the dummy was really in before, user: "opacne strany")
    std::memcpy(p2, e.data(), kInputBytes);
    std::memset(p1, 0, kInputBytes);
    g_take.push_back(e);
    g_t6_pr_frames.store(static_cast<uint32_t>(g_take.size()));
    if (g_take.size() >= kMaxFrames) StopRecording();
  } else if (g_mode == Mode::kPlay) {
    if (g_play_gap > 0) {
      g_play_gap--;
      std::memset(p2, 0, kInputBytes);
    } else {
      const auto& rec = g_recs[g_playing];
      if (g_play_at < rec.size()) std::memcpy(p2, rec[g_play_at].data(), kInputBytes);
      if (++g_play_at >= rec.size()) {
        g_play_at = 0;
        g_play_gap = 20;  // (between two runs: the dummy stands for a moment)
        NextRun();
      }
    }
  }
  if (static const bool log = T6Env("T6_PRACTICE_ENTRY_LOG") != nullptr; log) {
    static std::string last;
    std::string a, b;
    for (uint32_t k = 0; k < kEntrySize; k += 4) {
      a += fmt::format(" {:08X}", _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p1 + k)));
      b += fmt::format(" {:08X}", _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p2 + k)));
    }
    if (a + b != last) {
      REXLOG_INFO("T6 practice entry {}: P1{} | P2{}", g_battle_frames, a, b);
      last = a + b;
    }
  }
}

bool PracticeTakeRebattle() { return std::exchange(g_rebattle, false); }
void PracticeNewBattle() {
  g_start_taken = false;
  g_standing = 0;
  g_guarding = false;
}
