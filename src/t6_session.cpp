// Tekken 6 netplay - GGPO session (configuration, callbacks, practice pace, battle dump) (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- GGPO session (online plan step 4; port of the PS3 rb_net.cpp) ----
// Like SF4 Ember: the game runs its offline Versus, the pad reads of both players (XamInputGetState of users 0 / 1) get
// the inputs of the session. Frame structure of GGPO at the main loop head (T6FrameHook): advance the previous frame
// (GGPO saves the boundary through the ring), ggpo_idle (a rollback = load + re-simulated frames: the task tree walk
// sub_82218C80 is called again right here), add the local input, synchronize the inputs, then the frame runs.
//   T6_NET=1, T6_NET_PLAYER=1|2, T6_NET_LOCAL_PORT (7000), T6_NET_REMOTE=ip:port (127.0.0.1:7001), T6_NET_DELAY (1),
//   T6_NET_INPUT=live|random (local controller = user 0 of this PC | test pattern), T6_NET_SEED (agreed seed),
//   ggpo.network.delay=<ms> (GGPO's simulated latency).
// Session start (user: "synchronizace v loadingu"): the first frame boundary of the fight loading (task tree has
// tcb_gamemode_battle2d_t 0x8200C5E8). Both wait there for each other, set the agreed seeds and count frames from 0.
// Loading in the session is deterministic: before every frame the file request queue must be idle (LoaderIdle), so a
// request issued in frame N is done before frame N+1 on both instances. (Starting after the loading, v1/v2, gave
// different intros: the instance that waited for the peer had loaded more meanwhile.)
extern "C" void __imp__sub_82218C80(PPCContext& ctx, uint8_t* base);   // task tree walk = one frame
void T6StatusDisconnected();  // t6_status.cpp
extern "C" void __imp__XamInputGetState(PPCContext& ctx, uint8_t* base);
namespace t6np {

NetConfig& net_config_mut() {
  static NetConfig c = [] {
    NetConfig c;
    const auto env = [](const char* n) { const char* v = T6Env(n); return std::string(v ? v : ""); };
    c.enabled = env("T6_NET") == "1";
    if (auto v = env("T6_NET_PLAYER"); !v.empty()) c.player = T6ToInt(v.c_str()) == 2 ? 2 : 1;
    if (auto v = env("T6_NET_LOCAL_PORT"); !v.empty()) c.local_port = static_cast<uint16_t>(T6ToInt(v.c_str()));
    if (auto v = env("T6_NET_DELAY"); !v.empty()) c.delay = T6ToInt(v.c_str());
    if (auto v = env("T6_NET_SEED"); !v.empty()) c.seed = std::strtoul(v.c_str(), nullptr, 0);
    c.random_input = env("T6_NET_INPUT") == "random";
    c.attack_input = env("T6_NET_INPUT") == "attack";
    c.skip_intro = env("T6_NET_SKIPINTRO") == "1";  // (tests; the launcher passes 'skipintro=1' in the start line)
    if (auto v = env("T6_NET_REMOTE"); !v.empty()) c.SetRemote(v);
    return c;
  }();
  return c;
}
const NetConfig& net_config() { return net_config_mut(); }

GGPOSession* g_ggpo = nullptr;
GGPOPlayerHandle g_net_local = GGPO_INVALID_HANDLE, g_net_remote = GGPO_INVALID_HANDLE;
// (inputs GGPO drops when the input delay goes down - SetLocalDelay / KeepDroppedInput below)
int g_cur_delay = -1;   // input delay GGPO has for the local player (-1 = none yet in this session)
int g_drop_left = 0;    // local inputs GGPO will still drop after the last decrease
uint8_t g_drop_acc[2] = {};
bool g_frame_dropped = false;  // this frame's local input is one GGPO drops (presses of its input wait go to g_drop_acc)
bool g_net_started = false, g_net_running = false, g_net_resim = false, g_net_pending = false, g_net_valid = false;
int g_result_cursor = 0, g_park_downs = 2;
bool g_park = false, g_park_held = false, g_park_started = false, g_park_no_battle = false, g_park_new_battle = false;
uint32_t g_park_step0 = 0, g_park_confirm = 0;
uint32_t g_park_t1 = 0;  // pad step at which the battle task was found gone (0 = not yet)
bool g_park_wanted = false;     // 'park' command taken, the session not closed yet
uint32_t g_menu_frames = 0;     // session frames the result menu (game mode 27) has stood
// (g_watch_pause: declared with g_t6_holding)
int g_net_mode = 0;                      // 1 lockstep, -1 rollback (T6NetFrame); 0 = not set yet in this session
int g_net_max_pred = -1;                 // the prediction limit GGPO has now (1 = true lockstep: frames never re-simulated)
bool g_moku_match = false;               // Mokujin plays in this session (the rand() sync is only for his matches)
int g_net_depth = 0, g_frames_ahead = 0;
uint32_t g_net_frame = 0;  // GGPO frame being simulated (0 = first frame of the session)
uint64_t g_net_loads = 0, g_net_frames = 0, g_net_resim_frames = 0, g_net_wait_ms = 0;
uint8_t g_net_in[2][kNetInput] = {};
constexpr int kRing = 8;
SnapTest* g_ring[kRing] = {};
uint32_t g_ring_frame[kRing] = {};
double g_save_ms = 0, g_load_ms = 0, g_stats_t0 = 0, g_loader_wait_ms = 0;


// Task tree has both task types (vtables)
bool TaskTreeHas(uint32_t vt_a, uint32_t vt_b) {
  std::vector<uint32_t> todo{T6Rd32(0x82828C60u)};
  std::unordered_map<uint32_t, int> seen;
  bool a = false, b = false;
  while (!todo.empty() && seen.size() < 20000) {
    const uint32_t n = todo.back();
    todo.pop_back();
    if (!T6HeapPtr(n) || seen.count(n)) continue;
    seen[n] = 1;
    const uint32_t v = T6Rd32(n);
    a |= v == vt_a;
    b |= v == vt_b;
    if (a && b) return true;
    for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
  }
  return false;
}

// The task types (vtables) in the task tree, sorted (T6_PARK_TRACE: which screens exist at a pad step)
std::vector<uint32_t> TaskTreeVtables() {
  std::vector<uint32_t> todo{T6Rd32(0x82828C60u)}, out;
  std::unordered_map<uint32_t, int> seen;
  while (!todo.empty() && seen.size() < 20000) {
    const uint32_t n = todo.back();
    todo.pop_back();
    if (!T6HeapPtr(n) || seen.count(n)) continue;
    seen[n] = 1;
    const uint32_t v = T6Rd32(n);
    if (v >= 0x82000000u && v < 0x82170000u) out.push_back(v);
    for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}


// File request queue of NU::File::RequestManager (0x827F8F00, sub_82525280): pending list sentinel +10348, first node
// **(+10364); the worker waits on the semaphore *(+0x28) (and *(+0x48)) when it has nothing to do.
bool LoaderIdle() {
  const auto wf = T6Rex().waiters;
  constexpr uint32_t q = 0x827F8F00u;
  const uint32_t head = T6Rd32(q + 10364);
  if (head < 0x82000000u) return true;  // queue not set up yet (boot)
  const uint32_t first = T6Rd32(head);
  if (first != q + 10348) return false;
  // also no released-but-untaken count on the two loader semaphores (a request on its way between the requester and
  // the worker thread: the "bank ready" state of an MMC sound object flipped a frame apart, frame ~231, m3 / m6)
  const auto cnt = T6Rex().semaphore_count;
  const uint32_t h1 = T6Rd32(q + 0x28), h2 = T6Rd32(q + 0x48);
  const int w1 = wf ? wf(h1) : 1, w2 = wf ? wf(h2) : 1, c1 = cnt ? cnt(h1) : 0, c2 = cnt ? cnt(h2) : 0;
  static uint32_t logged = 0;
  if (logged < 6 && (state().step % 400) == 0) {
    logged++;
    REXLOG_INFO("T6 net: loader state step {}: waiters {} {} counts {} {}", state().step, w1, w2, c1, c2);
  }
  if (c1 > 0 || c2 > 0) return false;
  return w1 + w2 >= 1;
}
bool WaitLoaderIdle(int max_ms) {
  const double t0 = T6NowMs();
  while (!LoaderIdle()) {
    if (T6NowMs() - t0 > max_ms) return false;
    Sleep(1);
  }
  return true;
}

extern "C" PPCContext* t6_frame_ctx = nullptr;  // guest context of the running frame (T6_AWATCH register dump)
void RunFrame() {
  PPCContext tmp = *g_main_ctx;
  t6_frame_ctx = &tmp;
  tmp.r3.u64 = T6Rd32(0x82828C60u);
  __imp__sub_82218C80(tmp, g_t6_membase);
  t6_frame_ctx = nullptr;
}

// input latency test (input=step, 2026-10-17 "snizit input lag o 1 frame"): at the end of every boundary from session
// frame 990 on, player 1's move id (fight player +0x1C0) when it changed = the state the next drawn frame shows; with
// "T6 lat: forward first read" / "... in the synchronized input" the game's own frames from input to reaction
void T6LatMoveLog() {
  if (!net_config().step_input || g_net_frame < 990 || g_net_frame > 1100) return;
  static uint32_t prev = ~0u;
  const uint32_t pl = T6PlayerPtr(0);
  if (!((pl >= 0x82726000u && pl < 0x82DB0000u) || (pl >= 0xA7290000u && pl < 0xABA90000u))) return;  // (data or heap: 82900C00 in sessions)
  const uint32_t id = T6Rd32(pl + 0x1C0);
  if (id != prev) REXLOG_INFO("T6 lat: player 1 move {} at the end of the boundary after frame {}", id, g_net_frame);
  prev = id;
}

void SyncInputs() {
  int disconnect = 0;
  if (GGPO_SUCCEEDED(ggpo_synchronize_input(g_ggpo, g_net_in, sizeof(g_net_in), &disconnect))) {
    g_net_valid = true;
    if (net_config().step_input) {  // (input latency test: the frame that first runs with player 1's forward)
      static bool logged = false;
      if (!logged && (reinterpret_cast<const uint8_t*>(g_net_in)[1] & 0x08)) {
        logged = true;
        REXLOG_INFO("T6 lat: player 1 forward is in the synchronized input of the frame after {} ({})", g_net_frame, g_net_resim ? "re-simulated" : "live");
      }
    }
  } else {
    REXLOG_ERROR("T6 net: ggpo_synchronize_input failed");
  }
}

bool __cdecl cb_begin_game(const char*) { return true; }

// Desync check (PS3: fight coordinate system / positions + LCG): at every save the state of the frame is noted; frames
// 10 behind are confirmed (prediction max 3) and written to <dump dir>/fight_state.csv for comparing both instances.
// Positions: the object holding P1 (x, 1.0, z) and P2 0x40 after it (dump of rec_vs: 0xA849F040 +0x228 / +0x268);
// its heap address depends on the layout, so it is searched at the fight start (FindFightPositions).
std::map<uint32_t, std::string> g_state_rows;
std::map<uint32_t, std::vector<uint8_t>> g_player_rows;  // (T6_NET_PLAYER_DUMP)
uint32_t g_pos_addr = 0;  // address of P1 x (P2 x at +0x40)
// Round start: P1 / P2 stand symmetric (x1 = -x2, |x| > 100), y = 1.0 for both, z about equal
void FindFightPositions() {
  const auto fl = [](uint32_t a) { const uint32_t v = T6Rd32(a); float x; std::memcpy(&x, &v, 4); return x; };
  for (uint32_t a = 0xA7290000u; a + 0x50 < 0xABA90000u; a += 4) {
    const float y1 = fl(a + 4);
    if (y1 != 1.0f) continue;
    const float x1 = fl(a), z1 = fl(a + 8), x2 = fl(a + 0x40), y2 = fl(a + 0x44), z2 = fl(a + 0x48);
    if (y2 != 1.0f || !(std::fabs(x1) > 100.0f && std::fabs(x1) < 20000.0f)) continue;
    if (std::fabs(x1 + x2) < std::fabs(x1) * 0.05f && std::fabs(z1 - z2) < std::fabs(x1) * 0.1f && std::fabs(z1) > 1.0f) {
      g_pos_addr = a;
      REXLOG_INFO("T6 net: fight positions at {:08X}: P1 {:.1f},{:.1f} P2 {:.1f},{:.1f}", a, x1, z1, x2, z2);
      return;
    }
  }
  static int warned = 0;
  if (warned++ == 0) REXLOG_WARN("T6 net: fight positions not found yet");
}
void NoteFightState(uint32_t frame) {
  static FILE* f = [] { const char* d = T6Env("T6_NET_DUMP_DIR"); return d ? T6Open(fmt::format("{}/fight_state.csv", d), "w") : nullptr; }();
  if (!f) return;
  const auto fl = [](uint32_t a) { const uint32_t v = T6Rd32(a); float x; std::memcpy(&x, &v, 4); return x; };
  const uint32_t o = g_pos_addr;
  // + hash of the fight object area found in the session dump v21 (0xA7840800-0xA7840B00: (x, 1.0, z) pairs of both
  // players that move with the fight; same heap layout on both instances since the boot is deterministic)
  // Desync checksum: the XEX data/bss without the ranges that differ between two synchronous instances (m20 page
  // hashes, frames 150-260: job pool, file request queue, sound library statics, D3D, thread objects). Everything else
  // in data/bss was equal on both instances in every frame, so a difference here = the game state went apart.
  static const Range nondet[] = {{0x827A7000u, 0x827A8000u}, {0x827BF000u, 0x827C0000u}, {0x827D5000u, 0x827D7000u},
                                 {0x827F8000u, 0x827FC000u}, {0x827FD000u, 0x827FF000u}, {0x82829000u, 0x8282A000u},
                                 {0x828B3000u, 0x828B4000u}, {0x8297F000u, 0x82980000u}, {0x829C9000u, 0x829F1000u},
                                 {0x829F4000u, 0x829F5000u}, {0x82C65000u, 0x82C67000u}, {0x82CDF000u, 0x82CE0000u},
                                 {0x82CE1000u, 0x82CE2000u}, {0x82CE9000u, 0x82CEA000u}};
  // (6.5 MB hashed = ~1.0 ms per save, live and re-simulated; it only ever differed by the stage particle RNG, so it
  // is off unless T6_NET_AREA_HASH=1. The desync criterion is the players hash + the third RNG below.)
  static const bool area_on = T6Env("T6_NET_AREA_HASH") != nullptr;
  uint64_t area = 0xcbf29ce484222325ull;
  if (area_on) {
    uint32_t a = 0x82726000u;
    for (const auto& x : nondet) {
      if (x.lo > a) area = (area ^ Fnv(g_t6_membase + a, x.lo - a)) * 0x100000001b3ull;
      a = x.hi;
    }
    area = (area ^ Fnv(g_t6_membase + a, 0x82DB0000u - a)) * 0x100000001b3ull;
  }
  // Fight state proper: the two fight player objects (*(0x828DCE88), *(0x828DCE8C); 0xC40 bytes each: position,
  // move / animation state, health, rounds won at +112) and the third RNG (0x828D9F50), which the fight code draws
  // from. The main LCG (0x828B49E0) is the stage particle RNG (cosmetic, PS3 0x00D142AC) and not a desync criterion.
  uint64_t players = 0xcbf29ce484222325ull;
  for (const uint32_t pp : {0x828DCE88u, 0x828DCE8Cu}) {
    const uint32_t pl = T6Rd32(pp);
    if (T6PlayerObjectOk(pl)) {
      // Not hashed: +0x6B4, the one word that differed between a fresh game and a game parked again after a match
      // with neither side 0 (fp4, frame 5; it is rewritten in the fight). The words a game keeps from its previous
      // match are cleared at the session start (StartSession).
      // Not hashed either: +0x170, a heap pointer (another address on each side after the allocations of a long match
      // or a park, mk15 / pm4 2026-10-16: the only word that differed, with equal RNGs and the same winner).
      uint8_t tmp[0xC40];
      std::memcpy(tmp, g_t6_membase + pl, sizeof(tmp));
      std::memset(tmp + 0x6B4, 0, 4);
      std::memset(tmp + 0x170, 0, 4);
      players = (players ^ Fnv(tmp, sizeof(tmp))) * 0x100000001b3ull;
      // T6_NET_PLAYER_DUMP=1: the two player objects of every confirmed frame -> <dump dir>/players.bin (frame u32 +
      // 2 x 0xC40 bytes), to find WHICH bytes a players-hash difference is (Mokujin desync 2026-10-15)
      if (static const bool pd = T6Env("T6_NET_PLAYER_DUMP") != nullptr; pd) {
        auto& blob = g_player_rows[frame];
        if (pp == 0x828DCE88u) blob.assign(tmp, tmp + sizeof(tmp));
        else blob.insert(blob.end(), tmp, tmp + sizeof(tmp));
      }
    }
  }
  g_state_rows[frame] = (o ? fmt::format("{},{:.3f},{:.3f},{:.3f},{:.3f},{:08X},{:016X}", frame, fl(o), fl(o + 8), fl(o + 0x40),
                                         fl(o + 0x48), T6Rd32(0x828B49E0u), area)
                           : fmt::format("{},,,,,{:08X},{:016X}", frame, T6Rd32(0x828B49E0u), area)) +
                        fmt::format(",{:016X},{:08X},{:08X},{:08X}", players, T6Rd32(0x828D9F50u), T6PlayerPtr(0), T6PlayerPtr(1));
  if (static FILE* pf = [] {
        const char* d = T6Env("T6_NET_DUMP_DIR");
        return T6Env("T6_NET_PLAYER_DUMP") && d ? T6Open(fmt::format("{}/players.bin", d), "wb") : nullptr;
      }(); pf) {
    while (!g_player_rows.empty() && g_player_rows.begin()->first + 10 < frame) {
      const uint32_t fr = g_player_rows.begin()->first;
      const auto& b = g_player_rows.begin()->second;
      if (b.size() == 2 * 0xC40u) {
        std::fwrite(&fr, 4, 1, pf);
        std::fwrite(b.data(), 1, b.size(), pf);
      }
      g_player_rows.erase(g_player_rows.begin());
    }
    std::fflush(pf);
  }
  while (!g_state_rows.empty() && g_state_rows.begin()->first + 10 < frame) {
    std::fprintf(f, "%s\n", g_state_rows.begin()->second.c_str());
    g_state_rows.erase(g_state_rows.begin());
  }
  std::fflush(f);
}

void NetStatusError(const std::string& text);  // (below)
uint64_t g_resim_saves_skipped = 0;
bool g_net_skip_draw = false;  // this live frame is not drawn (it starts late: T6NetFrame)
uint32_t g_ft_skipped = 0;     // live frames of the window that were not drawn
double g_note_ms = 0;  // time in NoteFightState (desync check of the tests; not part of the save time)
// Whole rollbacks (PS3 "Rollback cost"): from the start of the load to the end of the last re-simulated frame, with the
// saves of the re-simulated frames
double g_rb_mark = 0, g_rb_cur = 0, g_rb_ms = 0, g_rb_max = 0;
// GGPO keeps only a token per saved frame (its frame number; the state is in the ring / the incremental store)
bool SaveToken(unsigned char** buffer, int* len, int* checksum, int frame) {
  *buffer = reinterpret_cast<unsigned char*>(new uint32_t[1]{static_cast<uint32_t>(frame)});
  *len = sizeof(uint32_t);
  *checksum = 0;
  return true;
}
bool __cdecl cb_save(unsigned char** buffer, int* len, int* checksum, int frame) {
  {
    const double n0 = T6NowMs();
    NoteFightState(static_cast<uint32_t>(frame));
    g_note_ms += T6NowMs() - n0;
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    for (uint32_t a : {0x82800000u, 0xA7300000u, 0xA9000000u, 0x41500000u}) {
      MEMORY_BASIC_INFORMATION mbi{};
      VirtualQuery(g_t6_membase + a, &mbi, sizeof(mbi));
      REXLOG_INFO("T6 net: page protection at {:08X}: {:X} (state {:X}, type {:X})", a, mbi.Protect, mbi.State, mbi.Type);
    }
  }
  const int slot = frame % kRing;
  // PS3 RB_ONLINE_RESIM_SAVE: inside a rollback no state is kept for a frame up to the last one whose inputs are all
  // known (g_ggpo_save_confirmed, sync.cpp) - GGPO only ever goes back to a frame with a predicted input, a later
  // one. The incremental store needs nothing for the gap: the pages written meanwhile stay marked and go into the
  // undo log of the next save. T6_NET_RESIM_SAVE_ALL=1 = every re-simulated frame is saved as before.
  if (static const bool all = T6Env("T6_NET_RESIM_SAVE_ALL") != nullptr;
      !all && g_ggpo_save_confirmed >= 0 && frame <= g_ggpo_save_confirmed && IncEnabled()) {
    g_resim_saves_skipped++;
    return SaveToken(buffer, len, checksum, frame);
  }
  if (!g_ring[slot]) g_ring[slot] = new SnapTest(true);
  const double t0 = T6NowMs();
  const char* const where0 = g_net_where.exchange("save");
  g_ring[slot]->Save(static_cast<uint32_t>(frame));
  g_net_where = where0;
  g_save_ms += T6NowMs() - t0;
  g_ring_frame[slot] = static_cast<uint32_t>(frame);
  return SaveToken(buffer, len, checksum, frame);
}

// Job queue of the pool 0x829CA080 (0x829EB9B4): used count +12, free count +36 (diagnostics around loads)
std::string JobQueueState() {
  constexpr uint32_t q = 0x829EB9B4u;
  const auto cnt = T6Rex().semaphore_count;
  const uint32_t sem = T6Rd32(0x829CA158u);
  // the pool 0x829CA080 holds four queues (sub_82518FA8: +0x20100, +0x21934 = q, +0x23168, +0x2499C completed), whose
  // nodes pass their +8 job buffer pointers between them (sub_8251BE60): the whole object 0x829CA080-0x829F0250 is kept
  // live (exclude file), saves/loads only when the workers are idle
  const auto qs = [](uint32_t o) { return fmt::format("{}/{}", T6Rd32(0x829CA080u + o + 12), T6Rd32(0x829CA080u + o + 36)); };
  return fmt::format("used {} free {} sem {} | queues used/free {} {} {} {}", T6Rd32(q + 12), T6Rd32(q + 36), cnt ? cnt(sem) : -2,
                     qs(0x20100), qs(0x21934), qs(0x23168), qs(0x2499C));
}

bool __cdecl cb_load(unsigned char* buffer, int) {
  const uint32_t frame = *reinterpret_cast<uint32_t*>(buffer);
  static int logged = 0;
  const std::string before = logged < 12 ? JobQueueState() : std::string();
  const int slot = frame % kRing;
  if (!g_ring[slot] || g_ring_frame[slot] != frame) {
    REXLOG_ERROR("T6 net: no snapshot of frame {} (slot holds {})", frame, g_ring_frame[slot]);
    NetStatusError(fmt::format("rollback state of frame {} missing", frame));  // (see below)
    return false;
  }
  // (and no load while the loader works on a request: PS3 crash of the FIOS thread at a rollback during a file read)
  if (!WaitLoaderIdle(5000)) REXLOG_WARN("T6 net: load of frame {}: file loader still busy after 5 s", frame);
  const double t0 = T6NowMs();
  g_rb_mark = t0;
  g_in_rollback = true;
  g_rb_cur = 0;
  const char* const where0 = g_net_where.exchange("load");
  T6VoicesBeforeLoad();
  const bool loaded = g_ring[slot]->Load(frame);
  g_net_where = where0;
  if (!loaded) {
    // (GGPO ignores the result: the game would go on from a state that is not that frame's = a desync for the rest of
    // the match. The launcher shows the error and ends the game; review 2026-10-18)
    NetStatusError(fmt::format("rollback state of frame {} missing", frame));
  }
  g_load_ms += T6NowMs() - t0;
  if (logged < 12) {
    logged++;
    REXLOG_INFO("T6 net: load of frame {} at frame {}: job queue before {} | after {}", frame, g_net_frame, before, JobQueueState());
  }
  g_net_frame = frame;
  g_net_loads++;
  return true;
}

double g_resim_ms = 0;  // time in the game step of re-simulated frames
bool __cdecl cb_log(char*, unsigned char*, int) { return true; }
void __cdecl cb_free(void* buffer) { delete[] static_cast<uint32_t*>(buffer); }

// Re-simulation of one frame inside a rollback (from ggpo_idle / ggpo_advance_frame)
double g_rbp[5] = {};  // rollback parts: sync inputs, game step, loader wait, advance (save), between frames
bool __cdecl cb_advance(int) {
  const double c0 = T6NowMs();
  g_rbp[4] += c0 - g_rb_mark;  // from the load / the previous re-simulated frame to here
  SyncInputs();
  g_rbp[0] += T6NowMs() - c0;
  g_net_resim = true;
  const double r0 = T6NowMs();
  const char* const where0 = g_net_where.exchange("re-simulated game frame");
  g_main_phase = 2;
  RunFrame();
  g_main_phase = 0;
  g_resim_ms += T6NowMs() - r0;
  g_rbp[1] += T6NowMs() - r0;
  // PS3 FIOS rule: no save with a file request on its way. The live boundary waits for the loader before its save; a
  // re-simulated frame that issued a request was saved with the request still in the pending list, the loader then
  // finished it, and a later rollback to that frame brought the pending entry back with nobody to take it: the next
  // loader wait never ended (pc5: frame 1931, "file loader still busy after 5 s" at every frame).
  {
    const double l0 = T6NowMs();
    g_net_where = "re-simulated frame: loader wait";
    if (!WaitLoaderIdle(5000)) REXLOG_WARN("T6 net: re-simulated frame {}: file loader still busy after 5 s", g_net_frame);
    g_loader_wait_ms += T6NowMs() - l0;
    g_rbp[2] += T6NowMs() - l0;
  }
  g_net_where = where0;
  g_net_resim = false;
  g_net_frame++;
  g_net_resim_frames++;
  const double a0 = T6NowMs();
  ggpo_advance_frame(g_ggpo);
  {
    const double now = T6NowMs();
    g_rbp[3] += now - a0;
    g_rb_ms += now - g_rb_mark;
    g_rb_cur += now - g_rb_mark;
    g_rb_max = std::max(g_rb_max, g_rb_cur);
    g_rb_mark = now;
  }
  return true;
}

bool __cdecl cb_event(GGPOEvent* ev) {
  switch (ev->code) {
    case GGPO_EVENTCODE_CONNECTED_TO_PEER: REXLOG_INFO("T6 net: connected to peer"); break;
    case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER: REXLOG_INFO("T6 net: synchronized with peer"); break;
    case GGPO_EVENTCODE_RUNNING: g_net_running = true; REXLOG_INFO("T6 net: session running"); break;
    case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
      REXLOG_ERROR("T6 net: disconnected from peer");
      T6StatusDisconnected();  // 'disconnected' in the T6_NET_STATUS file: the launcher ends the match
      break;
    case GGPO_EVENTCODE_TIMESYNC: g_frames_ahead = ev->u.timesync.frames_ahead; break;
    case GGPO_EVENTCODE_CONNECTION_INTERRUPTED: REXLOG_WARN("T6 net: connection interrupted"); break;
    case GGPO_EVENTCODE_CONNECTION_RESUMED: REXLOG_WARN("T6 net: connection resumed"); break;
    default: break;
  }
  return true;
}

// 'state error <text>' in the status file (T6_NET_STATUS): the launcher shows it and ends the game
void NetStatusError(const std::string& text) {
  if (const char* status = T6Env("T6_NET_STATUS"); status && *status) T6WriteFileAtomic(status, "state error " + text + "\n");
}
bool NetOwnPace() {
  static const bool on = T6Env("T6_NET_VSYNC") == nullptr;
  return on;
}
// The player's machine, once per run: a log sent by a player has to tell it by itself (user 2026-10-13: "ten vubec
// netusi ... musis to poznat sam z logu"). The graphics adapter is logged by the SDK at boot ("DXGI adapter").
void LogSystemOnce() {
  static bool done = false;
  if (done) return;
  done = true;
#if defined(_WIN32)
  char cpu[49] = {};
  int r[4] = {};
  __cpuid(r, static_cast<int>(0x80000000u));
  if (static_cast<unsigned>(r[0]) >= 0x80000004u) {
    for (int i = 0; i < 3; i++) {
      __cpuid(r, static_cast<int>(0x80000002u) + i);
      std::memcpy(cpu + 16 * i, r, 16);
    }
  }
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
  SYSTEM_POWER_STATUS ps{};
  GetSystemPowerStatus(&ps);
  REXLOG_INFO("T6 system: CPU '{}', {} threads, RAM {:.1f} GB ({:.1f} free), primary display {}x{} at {} Hz, {}", cpu,
              si.dwNumberOfProcessors, ms.ullTotalPhys / 1073741824.0, ms.ullAvailPhys / 1073741824.0, dm.dmPelsWidth,
              dm.dmPelsHeight, dm.dmDisplayFrequency,
              ps.BatteryFlag == 128 || ps.BatteryFlag == 255 ? "no battery" : ps.ACLineStatus == 1 ? "notebook on AC power" : "notebook ON BATTERY");
#else
  const long threads = ::sysconf(_SC_NPROCESSORS_ONLN);
  const long pages = ::sysconf(_SC_PHYS_PAGES);
  const long avail_pages = ::sysconf(_SC_AVPHYS_PAGES);
  const long page_size = ::sysconf(_SC_PAGESIZE);
  const double total_gb = pages > 0 && page_size > 0 ? static_cast<double>(pages) * page_size / 1073741824.0 : 0.0;
  const double avail_gb = avail_pages > 0 && page_size > 0 ? static_cast<double>(avail_pages) * page_size / 1073741824.0 : 0.0;
  std::string cpu = "Linux x86_64";
  if (FILE* f = std::fopen("/proc/cpuinfo", "r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
      if (std::strncmp(line, "model name", 10) == 0) {
        if (char* colon = std::strchr(line, ':')) {
          cpu = colon + 1;
          while (!cpu.empty() && (cpu.front() == ' ' || cpu.front() == '\t')) cpu.erase(cpu.begin());
          while (!cpu.empty() && (cpu.back() == '\n' || cpu.back() == '\r')) cpu.pop_back();
        }
        break;
      }
    }
    std::fclose(f);
  }
  REXLOG_INFO("T6 system: CPU '{}', {} threads, RAM {:.1f} GB ({:.1f} free), Linux native", cpu, threads, total_gb, avail_gb);
#endif
}
bool StartSession() {
  const NetConfig& c = net_config();
  LogSystemOnce();
  // A session after another one in the same process (park): nothing of the old session's bookkeeping may be left
  // The live save that a rollback would throw away at once is not made (GGPO fork: save after the poll, ~1.3 ms per
  // rollback). T6_NET_SAVE_FIRST=1 = stock GGPO order.
  g_ggpo_late_save = T6Env("T6_NET_SAVE_FIRST") ? 0 : 1;
  // T6_NET_PRIO=1 (experiment): no priority boost for woken threads in this process and the main thread one step
  // above the rest - a worker woken by a semaphore release must not push the main thread off its core.
  if (T6Env("T6_NET_PRIO")) {
#if defined(_WIN32)
    SetProcessPriorityBoost(GetCurrentProcess(), TRUE);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    REXLOG_INFO("T6 net: priority boost off, main thread above normal");
#else
    REXLOG_INFO("T6 net: priority override requested; Linux native keeps scheduler policy unchanged");
#endif
  }
  for (uint32_t& f : g_ring_frame) f = ~0u;
  g_state_rows.clear();
  g_pos_addr = 0;
  g_net_mode = 0;
  g_net_valid = g_net_pending = false;
  g_frames_ahead = 0;
  T6SndReset();
  // In a session the guest vblank runs every 1 ms instead of 16.7 ms (the cvar only sets the guest vblank period; the
  // host presents as before), so the swap of a frame is released at once and a load does not wait up to a vblank
  // period for it (WaitDrawIdle 0.5-13 ms -> ~0, nv1: load 13 -> 2 ms; PS3 RB_RSX_HOLD: the load waits ~0). The
  // session paces the frames itself (60 per second, T6NetFrame). T6_NET_VSYNC=1 = the guest vsync as before.
  if (NetOwnPace()) rex::cvar::SetFlagByName("vsync", "false");
  GGPOSessionCallbacks cb{};
  cb.begin_game = cb_begin_game;
  cb.save_game_state = cb_save;
  cb.load_game_state = cb_load;
  cb.log_game_state = cb_log;
  cb.free_buffer = cb_free;
  cb.advance_frame = cb_advance;
  cb.on_event = cb_event;
  // GGPO expects the application to have started Winsock (RPCS3 did on PS3; without it no socket -> sendto assert)
#if defined(_WIN32)
  WSADATA wsa{};
  if (const int err = WSAStartup(MAKEWORD(2, 2), &wsa); err != 0) REXLOG_ERROR("T6 net: WSAStartup failed: {}", err);
#endif
  if (!GGPO_SUCCEEDED(ggpo_start_session(&g_ggpo, &cb, const_cast<char*>("tekken6"), 2, kNetInput, c.local_port))) {
    REXLOG_ERROR("T6 net: ggpo_start_session failed");
    NetStatusError(fmt::format("cannot open UDP port {}", c.local_port));
    return false;
  }
  ggpo_set_disconnect_timeout(g_ggpo, 5000);
  ggpo_set_disconnect_notify_start(g_ggpo, 1000);
  for (int p = 1; p <= 2; p++) {
    GGPOPlayer pl{};
    pl.size = sizeof(pl);
    pl.player_num = p;
    GGPOPlayerHandle h = GGPO_INVALID_HANDLE;
    if (p == c.player) {
      pl.type = GGPO_PLAYERTYPE_LOCAL;
      ggpo_add_player(g_ggpo, &pl, &h);
      g_net_local = h;
    } else {
      pl.type = GGPO_PLAYERTYPE_REMOTE;
      // (strcpy_s ends the process on a too long string - an IPv6 address can be 45 characters, review 2026-10-18)
      if (c.remote_ip.size() >= sizeof(pl.u.remote.ip_address)) {
        REXLOG_ERROR("T6 net: opponent address too long for GGPO: {}", c.remote_ip);
        NetStatusError(fmt::format("opponent address not supported: {}", c.remote_ip));
        ggpo_close_session(g_ggpo);
        g_ggpo = nullptr;
        return false;
      }
      strcpy_s(pl.u.remote.ip_address, c.remote_ip.c_str());
      pl.u.remote.port = c.remote_port;
      ggpo_add_player(g_ggpo, &pl, &h);
      g_net_remote = h;
    }
  }
  g_cur_delay = -1;
  g_drop_left = 0;
  g_drop_acc[0] = g_drop_acc[1] = 0;
  SetLocalDelay(c.delay);
  REXLOG_INFO("T6 net: session at pad step {}: player {}, port {}, remote {}:{}, delay {}, input {}", state().step, c.player,
              c.local_port, c.remote_ip, c.remote_port, c.delay, c.random_input ? "random" : "live");
  const double t0 = T6NowMs();
  double note = t0;
  while (!g_net_running) {
    ggpo_idle(g_ggpo, 10);
    if (T6NowMs() - note > 10000) {
      note = T6NowMs();
      REXLOG_WARN("T6 net: waiting for the other player ({}:{})...", c.remote_ip, c.remote_port);
      // launcher start (hold point): after 30 s without the opponent the launcher is told, it ends the game
      if (T6Env("T6_NET_HOLD") && T6NowMs() - t0 > 30000) NetStatusError(fmt::format("no connection to the opponent {}:{}", c.remote_ip, c.remote_port));
    }
  }
  REXLOG_INFO("T6 net: peer ready after {:.0f} ms", T6NowMs() - t0);
  // Agreed start: replay off, seeds (main LCG 0x828B49E0, CRT rand of the main thread), write tracking from here
  state().play_done = true;
  *reinterpret_cast<uint32_t*>(g_t6_membase + 0x828B49E0u) = _byteswap_ulong(c.seed);
  {
    g_moku_match = T6MokuMatch();
    if (g_moku_match) REXLOG_INFO("T6 net: Mokujin in this match - rand() synced between the rounds");
  }
  if (const uint32_t ptd = T6MainPtd()) *reinterpret_cast<uint32_t*>(g_t6_membase + ptd + 20) = _byteswap_ulong(c.seed);
  // Kernel handles (0xF8xxxxxx) stored in the job pool control block and the NU draw object (the worker waits use them)
  for (const auto& [lo, hi] : {std::pair{0x829CA000u, 0x829CA200u}, std::pair{0x82C66100u, 0x82C66300u}, std::pair{0x829EB980u, 0x829EBA00u}}) {
    std::string h;
    for (uint32_t a = lo; a < hi; a += 4) {
      const uint32_t v = T6Rd32(a);
      if ((v >> 24) == 0xF8) h += fmt::format(" {:08X}={:08X}", a, v);
    }
    REXLOG_INFO("T6 net: handles in {:08X}-{:08X}:{}", lo, hi, h);
  }
  // Fight player objects (*(0x828DCE88), *(0x828DCE8C), 0xC40 bytes): the battle init leaves ~42 words as the last
  // match left them (hit positions, pointers to objects of the old match, counters). A fresh game has 0 there. fp1-fp4
  // (a fresh game against one parked again): the fight state and the third RNG stayed equal, these words differed
  // until the fight rewrote them (frames 0-1135). They are cleared here, at the same point on both sides, so both
  // start from the bytes of a fresh game. Offsets = union of both players in fp4 (frame 5).
  {
    static const uint16_t stale[] = {0x074, 0x140, 0x148, 0x14C, 0x1FC, 0x3A8, 0x680, 0x684, 0x730, 0x740, 0x744, 0x768, 0x78C,
                                     0x7B4, 0x7B8, 0x7BC, 0x7C0, 0x7C4, 0x7C8, 0x7CC, 0x7D0, 0x7D4, 0x7D8, 0x7DC, 0x7E0,
                                     0x9C4, 0x9C8, 0x9CC, 0x9D0, 0x9D4, 0x9D8, 0x9E4, 0xAB8, 0xABC, 0xAC0, 0xAC4, 0xAC8, 0xACC,
                                     0xB20, 0xB24, 0xB48, 0xB5C, 0xB60, 0xB64};
    uint32_t cleared = 0;
    for (const uint32_t pp : {0x828DCE88u, 0x828DCE8Cu}) {
      const uint32_t pl = T6Rd32(pp);
      if (!T6PlayerObjectOk(pl)) continue;
      for (const uint16_t o : stale) {
        uint32_t& w = *reinterpret_cast<uint32_t*>(g_t6_membase + pl + o);
        cleared += w != 0;
        w = 0;
      }
    }
    REXLOG_INFO("T6 net: session start: {} words of the previous match cleared in the fight player objects", cleared);
  }
  if (!WaitLoaderIdle(10000)) REXLOG_WARN("T6 net: file loader still busy at the session start");
  ResetWriteTracking();
  g_net_frame = 0;
  g_stats_t0 = T6NowMs();
  g_park = false;
  g_watch_pause = false;
  return true;
}

// Local input: XINPUT_GAMEPAD of user 0 of this PC (guest byte order), or a test pattern (random buttons / directions,
// held 1-15 frames; never START / BACK)
bool g_practice_battle = false;  // T6_PRACTICE: the practice battle exists (fast start off, real time)

// Button layout of the launcher (Profile -> Controls): T6_PAD_MAP=<name>=<hex wButtons>,... for the pad buttons A B X Y
// LB RB LT RT LS RS BACK START; the value replaces what the button does (0 = nothing, 4000 = X = left punch, C000 =
// 1+2 ...). A button not in the list keeps its own bit. LT / RT count as pressed from half travel and lose their
// analog value when mapped. g = XINPUT_GAMEPAD in guest byte order (wButtons big endian, then LT, RT).
void RemapPad(uint8_t* g) {
  struct Src { const char* name; uint16_t bit; int trigger; };  // trigger: 0 none, 1 LT (g[2]), 2 RT (g[3])
  static const Src kSrc[12] = {{"A", 0x1000, 0}, {"B", 0x2000, 0}, {"X", 0x4000, 0}, {"Y", 0x8000, 0}, {"LB", 0x0100, 0},
                               {"RB", 0x0200, 0}, {"LT", 0, 1}, {"RT", 0, 2}, {"LS", 0x0040, 0}, {"RS", 0x0080, 0},
                               {"BACK", 0x0020, 0}, {"START", 0x0010, 0}};
  static int map[12];
  static const bool any = [] {
    std::fill(std::begin(map), std::end(map), -1);
    const char* e = T6Env("T6_PAD_MAP");
    if (!e || !*e) return false;
    bool some = false;
    T6ForEachPair(e, '=', [&](const std::string& name, const std::string& value) {
      for (int i = 0; i < 12; i++) {
        if (name == kSrc[i].name) {
          map[i] = static_cast<int>(std::strtoul(value.c_str(), nullptr, 16) & 0xFFFF);
          some = true;
        }
      }
    });
    REXLOG_INFO("T6 pad map: {}", e);
    return some;
  }();
  if (!any) return;
  const uint16_t in = static_cast<uint16_t>(g[0] << 8 | g[1]);
  const uint8_t trig[2] = {g[2], g[3]};
  uint16_t out = in;
  for (int i = 0; i < 12; i++) {  // the mapped buttons lose their own meaning
    if (map[i] < 0) continue;
    if (kSrc[i].trigger) g[1 + kSrc[i].trigger] = 0;
    else out &= ~kSrc[i].bit;
  }
  for (int i = 0; i < 12; i++) {  // every pressed mapped button adds its value
    if (map[i] < 0) continue;
    const bool down = kSrc[i].trigger ? trig[kSrc[i].trigger - 1] >= 128 : (in & kSrc[i].bit) != 0;
    if (down) out |= static_cast<uint16_t>(map[i]);
  }
  g[0] = static_cast<uint8_t>(out >> 8);
  g[1] = static_cast<uint8_t>(out);
}

// Presses between two frames (2026-10-15, players: "it eats inputs"): the pad and keyboard were read once per frame,
// right before the input wait, so a press made and let go between two reads - or during a wait for the peer - never
// reached the game. LocalPoll() reads them again during the waits of the frame (pacing wait, input wait, about every
// millisecond) and keeps every button / direction newly pressed since the last frame's input; LocalLatched() adds them
// to the frame's input once. T6_NET_LATCH=0 = off (the old one read per frame).
uint8_t g_latch[2] = {}, g_last_sent[2] = {};
std::atomic<uint64_t> g_in_reads{0}, g_in_frames{0};  // T6_INPUT_DIAG (InputDiagThread below)
std::atomic<uint64_t> g_in_delivered{0};  // presses (new bits of the buttons / d-pad bytes) in the frames' inputs
std::atomic<double> g_in_last_read_ms{0};
// The left stick the same way (2026-10-16, players: inputs still missing): of the reads since the last frame the one
// farthest from the middle per axis (bytes 4-5 x, 6-7 y, big endian), used when the frame's own read is nearer the
// middle and that one is past the XInput dead zone - a quick flick of the stick between two frames.
int16_t g_stick_peak[2] = {};
int16_t Be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }
bool LatchOn() {
  static const bool on = T6EnvDefaultOn("T6_NET_LATCH");
  return on;
}
void LocalPoll() {
  if (!LatchOn()) return;
  const NetConfig& c = net_config();
  if (c.random_input || c.step_input || c.attack_input) return;
  uint8_t now[kNetInput];
  LocalInput(now);
  g_latch[0] |= now[0] & ~g_last_sent[0];
  g_latch[1] |= now[1] & ~g_last_sent[1];
  for (int a = 0; a < 2; a++)
    if (const int16_t v = Be16(now + 4 + 2 * a); std::abs(v) > std::abs(g_stick_peak[a])) g_stick_peak[a] = v;
}
// the frame's input: the read of now plus the presses latched since the last frame (a direction latched against the
// one held now - left + right, up + down - keeps the held one)
// presses (buttons byte, d-pad byte) added to an input; a direction pair that would end up both pressed (left + right,
// up + down) keeps what the input itself holds on that axis (both or none held: neutral)
void MergePresses(uint8_t* local, uint8_t b0, uint8_t b1) {
  uint8_t d = local[1] | b1;
  for (const uint8_t axis : {uint8_t{0x0C}, uint8_t{0x03}})
    if ((d & axis) == axis) d = (d & ~axis) | ((local[1] & axis) == axis ? 0 : (local[1] & axis));
  local[0] |= b0;
  local[1] = d;
}
// Inputs dropped by GGPO when the input delay goes down (user 2026-10-15: "ztracene stisky na FIGHT vyresit"). The
// switch lockstep (delay 4) -> fight (delay 1) at "FIGHT": the queue already holds this side's inputs up to 3 frames
// ahead, so GGPO's InputQueue::AdvanceQueueHead tosses the next 3 local inputs ("Dropping input frame") - a press in
// the first ~50 ms of the round vanished. Now the dropped frames' buttons are kept and merged into the first frame
// GGPO takes again: the press comes up to 3 frames later instead of never.
void SetLocalDelay(int d) {
  if (g_cur_delay >= 0 && d < g_cur_delay) {
    g_drop_left = g_cur_delay - d;
    REXLOG_INFO("T6 net: frame {}: input delay {} -> {}: the next {} local inputs are kept for frame {}", g_net_frame,
                g_cur_delay, d, g_drop_left, g_net_frame + g_drop_left);
  }
  g_cur_delay = d;
  ggpo_set_frame_delay(g_ggpo, g_net_local, d);
}
// (right before the frame's input goes to GGPO)
void KeepDroppedInput(uint8_t* local) {
  if (static const bool off = T6EnvIs("T6_NET_KEEP_DROPPED", '0'); off) return;
  g_frame_dropped = g_drop_left > 0;
  if (g_drop_left > 0) {
    g_drop_acc[0] |= local[0];
    g_drop_acc[1] |= local[1];
    g_drop_left--;
    return;
  }
  if (g_drop_acc[0] | g_drop_acc[1]) {
    MergePresses(local, g_drop_acc[0], g_drop_acc[1]);
    g_drop_acc[0] = g_drop_acc[1] = 0;
  }
}
// Mokujin desync (user 2026-10-15; server: 131 of the 145 desyncs since 10-14 had Mokujin, 21 % of his matches):
// between the rounds Mokujin draws the style of the next round (fight player +0x54) from the CRT rand() of the main
// thread - a stream that the two games do not share: sub_821BA860 (stage objects' random choice, 99 % of all rand()
// calls) draws it a different number of times on each side (mk6, frame 3060: 1346 vs 1350). Two parts:
//  - sub_821BA860 draws from its own state (below), so it no longer moves the shared one;
//  - every live frame outside the fight proper (intros, KO, between rounds: lockstep, no rollback) starts with the
//    shared state set from the agreed seed and the frame number, the same on both sides.
// The fight proper draws from the third RNG (agreed seed), not from rand(). Only in matches with Mokujin (user
// 2026-10-15: "nedotkne se to niceho jineho? jen mokujina"): every other match runs as before. T6_NET_CRT_SYNC=0 = off.
uint32_t g_crt_ptd = 0;  // the main thread's CRT per-thread data (+20 = rand state)
uint32_t CrtPtd() {
  if (!g_crt_ptd) g_crt_ptd = T6MainPtd();
  return g_crt_ptd;
}
void CrtRandSync() {
  static const bool off = T6EnvIs("T6_NET_CRT_SYNC", '0');
  // (true lockstep only - prediction 1: such frames are never re-simulated, so every run of the frame sees the value;
  // the KO grace frames predict and may be re-simulated, they keep the stream as it is)
  if (off || !g_moku_match || g_net_mode != 1 || g_net_max_pred != 1) return;
  if (const uint32_t ptd = CrtPtd()) {
    uint32_t h = net_config().seed ^ (g_net_frame * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    *reinterpret_cast<uint32_t*>(g_t6_membase + ptd + 20) = _byteswap_ulong(h);
    t6_dirty_mark(ptd + 20);
  }
}
void LocalLatched(uint8_t* local) {
  if (LatchOn()) {
    MergePresses(local, g_latch[0], g_latch[1]);
    for (int a = 0; a < 2; a++) {
      const int16_t peak = g_stick_peak[a];
      if (std::abs(peak) > 7849 && std::abs(peak) > std::abs(Be16(local + 4 + 2 * a))) {
        local[4 + 2 * a] = static_cast<uint8_t>(static_cast<uint16_t>(peak) >> 8);
        local[5 + 2 * a] = static_cast<uint8_t>(peak);
      }
    }
  }
  g_stick_peak[0] = g_stick_peak[1] = 0;
  if (static const bool diag = T6Env("T6_INPUT_DIAG") != nullptr; diag)
    g_in_delivered.fetch_add(__popcnt((local[0] & ~g_last_sent[0]) & 0xFF) + __popcnt((local[1] & ~g_last_sent[1]) & 0x0F));
  g_latch[0] = g_latch[1] = 0;
  g_in_frames.fetch_add(1);
  g_last_sent[0] = local[0];
  g_last_sent[1] = local[1];
}

// T6_INPUT_DIAG=1 (measurement, user 2026-10-17: "ta hra zere inputy" on a WinMM arcade stick): a thread reads the
// stick itself every millisecond (joyGetPosEx, T6_INPUT_DIAG_JOY = device index, default 0) and checks every press
// against the game's own reads: "unseen" = pressed and released with no read of LocalInput in between (the game never
// had it), "double" = a second press of the same button / direction before the frame's input went out (merged into one
// press). Every 10 s: "T6 input diag: ..." in the log; every unseen press with its length.
#if defined(_WIN32)
void InputDiagThread() {
  using JoyGetPosEx = UINT(WINAPI*)(UINT, LPJOYINFOEX);
  HMODULE mm = LoadLibraryA("winmm.dll");
  const auto get = mm ? reinterpret_cast<JoyGetPosEx>(GetProcAddress(mm, "joyGetPosEx")) : nullptr;
  if (!get) return;
  const UINT joy = T6Env("T6_INPUT_DIAG_JOY") ? static_cast<UINT>(T6ToInt(T6Env("T6_INPUT_DIAG_JOY"))) : 0;
  // (Sleep(1) is 1 ms: the netplay pacing already set the timer resolution of the process)
  constexpr int kBits = 36;  // 32 buttons, POV up / right / down / left
  bool down[kBits] = {};
  uint64_t reads_at[kBits] = {}, frame_at[kBits] = {};
  bool pressed_in_frame[kBits] = {};
  double t_down[kBits] = {};
  uint64_t presses = 0, unseen = 0, dbl = 0, dir_presses = 0, gap_max_ms = 0;
  uint64_t tot_presses = 0, tot_unseen = 0, tot_dbl = 0, delivered0 = g_in_delivered.load(), win_delivered0 = delivered0;
  double next_log = T6NowMs() + 10000;
  REXLOG_INFO("T6 input diag: reading WinMM joystick {} every ms", joy + 1);
  for (;;) {
    JOYINFOEX info{};
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNBUTTONS | JOY_RETURNPOV;
    const double now = T6NowMs();
    if (get(joy, &info) == JOYERR_NOERROR) {
      bool cur[kBits] = {};
      for (int b = 0; b < 32; b++) cur[b] = (info.dwButtons >> b) & 1;
      if (info.dwPOV != JOY_POVCENTERED && info.dwPOV <= 35900)
        for (int d = 0; d < 4; d++) {
          float diff = std::fabs(static_cast<float>(info.dwPOV) - d * 9000.0f);
          diff = std::min(diff, 36000.0f - diff);
          cur[32 + d] = diff <= 6750.0f;
        }
      const uint64_t reads = g_in_reads.load(), frames = g_in_frames.load();
      for (int i = 0; i < kBits; i++) {
        if (cur[i] && !down[i]) {
          if (pressed_in_frame[i] && frame_at[i] == frames) dbl++, tot_dbl++;
          down[i] = true;
          reads_at[i] = reads;
          frame_at[i] = frames;
          pressed_in_frame[i] = true;
          t_down[i] = now;
        } else if (!cur[i] && down[i]) {
          down[i] = false;
          presses++, tot_presses++;
          if (i >= 32) dir_presses++;
          if (reads == reads_at[i]) {
            unseen++, tot_unseen++;
            REXLOG_INFO("T6 input diag: UNSEEN press of {} {} held {:.0f} ms, frame {} (last game read {:.0f} ms before the press)",
                        i < 32 ? "button" : "direction", i < 32 ? i + 1 : i - 31, now - t_down[i], g_net_frame,
                        t_down[i] - g_in_last_read_ms.load());
          }
        }
      }
      // the longest stretch without a game read
      if (const double lr = g_in_last_read_ms.load(); lr > 0 && g_net_running) gap_max_ms = std::max<uint64_t>(gap_max_ms, static_cast<uint64_t>(now - lr));
    }
    if (now >= next_log) {
      next_log = now + 10000;
      const uint64_t dl = g_in_delivered.load();
      if (presses || unseen || dbl)
        REXLOG_INFO("T6 input diag: last 10 s: {} presses ({} directions), {} unseen by a read, {} double taps in one frame, "
                    "{} presses in the frames' inputs; longest stretch without a read {} ms; so far {} presses, {} unseen, {} double, "
                    "{} in the inputs", presses, dir_presses, unseen, dbl, dl - win_delivered0, gap_max_ms, tot_presses, tot_unseen,
                    tot_dbl, dl - delivered0);
      win_delivered0 = dl;
      presses = unseen = dbl = dir_presses = gap_max_ms = 0;
    }
    Sleep(1);
  }
}

#else
void InputDiagThread() { REXLOG_WARN("T6 input diag: WinMM diagnostic sampler is unavailable on Linux native"); }
#endif

// T6_PROFILE=1 (2026-10-17, user: "jdi na profilovani"): a thread stops the main (game) thread every millisecond and
// notes where it is - the recompiled guest function (PPCFuncMappings: nearest host start below the address), else the
// module (runtime / GPU / system DLL) - separately for live and re-simulated (rollback) frames. Every 30 s the top 30 of
// each go to the log ("T6 profile"). Nothing is allocated while the thread is stopped (no lock it might hold).
#if defined(_WIN32)
std::atomic<HANDLE> g_prof_main{nullptr};
void ProfileThread() {
  std::vector<std::pair<uintptr_t, uint32_t>> fns;  // host start -> guest address
  for (const PPCFuncMapping* m = PPCFuncMappings; m->guest; m++) fns.emplace_back(reinterpret_cast<uintptr_t>(m->host), static_cast<uint32_t>(m->guest));
  std::sort(fns.begin(), fns.end());
  HMODULE exe = GetModuleHandleW(nullptr);
  std::unordered_map<uint32_t, uint64_t> live, resim;  // guest function (or module id | 0xFFFF0000) -> samples
  std::unordered_map<uint32_t, std::string> modname;
  uint64_t n_live = 0, n_resim = 0;
  double next = T6NowMs() + 30000;
  REXLOG_INFO("T6 profile: sampling the main thread every ms ({} guest functions)", fns.size());
  for (;;) {
    Sleep(1);
    HANDLE h = g_prof_main.load();
    if (!h) continue;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(h) == static_cast<DWORD>(-1)) continue;
    const bool ok = GetThreadContext(h, &c);
    const bool rs = g_net_resim;
    ResumeThread(h);
    if (!ok) continue;
    const uintptr_t rip = c.Rip;
    uint32_t key = 0;
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(rip), &mod);
    auto it = std::upper_bound(fns.begin(), fns.end(), std::make_pair(rip, 0xFFFFFFFFu));
    if (mod == exe && it != fns.begin() && rip - std::prev(it)->first < 0x40000) {
      key = std::prev(it)->second;
    } else {
      key = 0xFFFF0000u | (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mod) >> 16) & 0xFFFF);
      if (!modname.count(key)) {
        wchar_t w[MAX_PATH] = {};
        char a[MAX_PATH] = {};
        if (mod) GetModuleFileNameW(mod, w, MAX_PATH);
        const wchar_t* b = wcsrchr(w, L'\\');
        WideCharToMultiByte(CP_UTF8, 0, b ? b + 1 : w, -1, a, sizeof(a) - 1, nullptr, nullptr);
        modname[key] = mod == exe ? std::string("tekken6.exe (host code)") : mod ? std::string(a) : std::string("(no module)");
      }
    }
    (rs ? resim : live)[key]++;
    (rs ? n_resim : n_live)++;
    if (T6NowMs() >= next) {
      next = T6NowMs() + 30000;
      for (int k = 0; k < 2; k++) {
        auto& m = k ? resim : live;
        const uint64_t tot = k ? n_resim : n_live;
        if (!tot) continue;
        std::vector<std::pair<uint64_t, uint32_t>> v;
        v.reserve(m.size());
        for (const auto& [f, cnt] : m) v.emplace_back(cnt, f);
        std::sort(v.rbegin(), v.rend());
        std::string line;
        for (size_t i = 0; i < v.size() && i < 30; i++) {
          const uint32_t f = v[i].second;
          line += fmt::format(" {}={:.1f}%", f >= 0xFFFF0000u ? modname[f] : fmt::format("sub_{:08X}", f), v[i].first * 100.0 / tot);
        }
        REXLOG_INFO("T6 profile {} ({} samples):{}", k ? "re-simulated" : "live", tot, line);
      }
    }
  }
}

#else
std::atomic<T6ProfileHandle> g_prof_main{0};
void ProfileThread() { REXLOG_WARN("T6 profile: host thread suspend sampler unavailable on Linux native"); }
#endif

void LocalInput(uint8_t* out) {
  std::memset(out, 0, kNetInput);
  const NetConfig& c = net_config();
  if (c.random_input) {
    static uint32_t rng = 0x9E3779B9u * static_cast<uint32_t>(c.player);
    static uint16_t buttons = 0;
    static int hold = 0;
    const auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    if (--hold <= 0) {
      static const uint16_t dirs[] = {0, 0, 0x4, 0x8, 0x1, 0x2, 0x5, 0x9, 0x6, 0xA};
      static const uint16_t face[] = {0, 0, 0x1000, 0x2000, 0x4000, 0x8000, 0x3000, 0xC000};
      buttons = dirs[next() % 10] | face[next() % 8];
      hold = 1 + next() % 15;
    }
    out[0] = static_cast<uint8_t>(buttons >> 8);
    out[1] = static_cast<uint8_t>(buttons);
    return;
  }
  if (c.step_input) {
    if (c.player == 1 && g_net_frame >= 1000) {
      out[1] = 0x08;  // DPAD_RIGHT
      if (static bool logged = false; !logged) {
        logged = true;
        REXLOG_INFO("T6 lat: forward first read at the boundary after frame {} (delay {}: it is the input of frame {})", g_net_frame,
                    c.delay, g_net_frame + 1 + c.delay);
      }
    }
    return;
  }
  if (c.attack_input) {
    // player 1: walks forward and punches (X) 4 frames of every 14; player 2: nothing
    static uint32_t n = 0;
    if (c.player == 1) { out[1] = 0x08; if ((n++ % 14) < 4) out[0] = 0x40; }  // DPAD_RIGHT held (walk forward) + X 0x4000
    else out[1] = 0x04;  // player 2 walks forward too (DPAD_LEFT): standing still would guard every punch
    return;
  }
  if (static const bool diag = T6Env("T6_INPUT_DIAG") != nullptr; diag) {
    static std::once_flag once;
    std::call_once(once, [] { std::thread(InputDiagThread).detach(); });
    g_in_reads.fetch_add(1);
    g_in_last_read_ms.store(T6NowMs());
  }
  static uint32_t scratch = g_t6_sys_alloc ? g_t6_sys_alloc(32, 16) : 0;
  if (scratch) {
    PPCContext tmp = *g_main_ctx;
    tmp.r3.u64 = 0;
    tmp.r4.u64 = 1;
    tmp.r5.u64 = scratch;
    __imp__XamInputGetState(tmp, g_t6_membase);
    if (tmp.r3.u32 == 0) {
      std::memcpy(out, g_t6_membase + scratch + 4, 12);
      RemapPad(out);
    }
  }
  // Keyboard (added to the controller): T6_NET_KEYS=1 (default) W A S D + U I J K (left punch, right punch, left kick,
  // right kick = X Y A B), Enter = START; T6_NET_KEYS=2 arrows + numpad 4 5 1 2, numpad Enter... = START (two players on
  // one keyboard for local tests); 0 = off. Only while this game's window is the foreground window, unless
  // T6_NET_KEYS_GLOBAL=1 (two local instances at once).
  const uint16_t b = KeyboardButtons();
  out[0] |= static_cast<uint8_t>(b >> 8);
  out[1] |= static_cast<uint8_t>(b);
}

// A key held now, or pressed and let go since the last look at it (user 2026-10-17: a press of a few ms between two
// reads has to count): GetAsyncKeyState's low bit. The first look at a key only primes it, and while the game's window
// is not in front the keys are looked at (cleared) but count nothing - no old press from another program comes in.
#if defined(_WIN32)
bool KeyDown(int vk, bool focus) {
  static bool primed[256] = {};
  const SHORT st = GetAsyncKeyState(vk);
  const bool was_primed = primed[vk & 0xFF];
  primed[vk & 0xFF] = true;
  if (!focus) return false;
  return (st & 0x8000) || (was_primed && (st & 1));
}

// XINPUT wButtons from the keyboard (T6_NET_KEYS layouts below), 0 when off or the game's window is not in front
uint16_t KeyboardButtons() {
  uint16_t b = 0;
  static const int keys = T6EnvInt("T6_NET_KEYS", 1);
  // T6_NET_KEYS=3: the player's own layout from the launcher, T6_KEYMAP=<vk hex>:<wButtons hex>,... (CONTROLS, Custom)
  struct Key { int vk; uint16_t bit; };
  static const std::vector<Key> custom = [] {
    std::vector<Key> out;
    T6ForEachPair(T6Env("T6_KEYMAP"), ':', [&](const std::string& vk, const std::string& bits) {
      out.push_back({static_cast<int>(std::strtoul(vk.c_str(), nullptr, 16)), static_cast<uint16_t>(std::strtoul(bits.c_str(), nullptr, 16))});
    });
    return out;
  }();
  if (keys == 1 || keys == 2 || keys == 3) {
    static const bool global = T6Env("T6_NET_KEYS_GLOBAL") != nullptr;
    bool focus = global;
    if (!focus) {
      DWORD pid = 0;
      if (const HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &pid);
      focus = pid == GetCurrentProcessId();
    }
    {
      if (keys == 3) {
        for (const Key& k : custom)
          if (KeyDown(k.vk, focus)) b |= k.bit;
        return b;
      }
      static const Key set1[] = {{'W', 0x0001}, {'S', 0x0002}, {'A', 0x0004}, {'D', 0x0008}, {VK_RETURN, 0x0010}, {VK_BACK, 0x0020},
                                 {'J', 0x1000}, {'K', 0x2000}, {'U', 0x4000}, {'I', 0x8000}};
      static const Key set2[] = {{VK_UP, 0x0001}, {VK_DOWN, 0x0002}, {VK_LEFT, 0x0004}, {VK_RIGHT, 0x0008}, {VK_ADD, 0x0010}, {VK_SUBTRACT, 0x0020},
                                 {VK_NUMPAD1, 0x1000}, {VK_NUMPAD2, 0x2000}, {VK_NUMPAD4, 0x4000}, {VK_NUMPAD5, 0x8000}};
      for (const Key& k : keys == 1 ? set1 : set2) {
        if (KeyDown(k.vk, focus)) b |= k.bit;
      }
    }
  }
  return b;
}
#else
bool KeyDown(int, bool) { return false; }
uint16_t KeyboardButtons() {
  static bool noted = false;
  const int keys = T6EnvInt("T6_NET_KEYS", 1);
  if (keys && !noted) {
    noted = true;
    REXLOG_WARN("T6 keyboard fallback: Win32 GetAsyncKeyState path is not yet wired on Linux native; controller/net input remains active");
  }
  return 0;
}
#endif
}  // namespace t6np

// The practice paced like the online fight (user 2026-10-14: a video of the same combo in RPCS3 and in our practice -
// it dropped in ours; "je hloupe aby practice a hra byly jiny"). Outside a session the frames were let go by the sound
// device (the audio step waits for room in its queue; the device takes ~2 audio frames every ~10.7 ms) under the
// guest vsync: frame periods 11 / 16 / 21 ms with a right average, so a press landed a frame earlier or later than in
// RPCS3 (its frames come from a 60 Hz flip timer). Now the practice fight uses the session's frame clock: guest vblank
// 1 ms (cvar vsync off), exactly 60 frames per second from QueryPerformanceCounter. Without the session's correction
// from the sound queue depth: practice has no audio lockstep (AudioStep / rex_audio_advance run only in a session), the
// sound worker runs free on its own thread and keeps the queue full (7.8 of 8) whatever the frames do - the correction
// saturated at +0.25 ms = 59.1 fps (T6_PRACTICE_PACE=2 keeps it, tests). Measured (pace_0 / pace_1, 2026-10-14):
// sound paced 150-167 of 600 periods over 17.5 ms, max ~20 ms; own clock 0 over 17.5 ms, max 16.7, avg 16.67.
// T6_PRACTICE_PACE=0 = as before. Every 600 frames "T6 practice frames" = periods of the last 600 frames.
void PracticePace() {
  static const bool practice = T6Env("T6_PRACTICE") != nullptr;
  if (!practice || !g_practice_battle) return;
  static const bool on = T6EnvDefaultOn("T6_PRACTICE_PACE");
  static double next = 0, depth_avg = -1, last = 0, sum = 0, mx = 0;
  static int n = 0, over175 = 0, over20 = 0, vsync_set = 0;
  const auto queued = T6Rex().audio_queued;
  if (on) {
    if (vsync_set < 10) {  // (the practice's fast start switches the guest vsync on when the battle begins)
      rex::cvar::SetFlagByName("vsync", "false");
      vsync_set++;
    }
    const double now = T6NowMs();
    if (next == 0 || now > next + 50.0) next = now;
    while (true) {
      const double left = next - T6NowMs();
      if (left <= 0) break;
      if (left > 2.0) Sleep(static_cast<DWORD>(left - 1.5)); else YieldProcessor();
    }
    double period = 1000.0 / 60.0;
    // (T6_PRACTICE_PACE=2: + the session's correction from the sound queue depth; tests)
    static const bool correct = T6EnvIs("T6_PRACTICE_PACE", '2');
    if (queued && correct) period += std::clamp((depth_avg - 4.0) * 0.08, -0.25, 0.25);
    next += period;
  }
  if (queued) {
    const int q = queued();
    depth_avg = depth_avg < 0 ? q : depth_avg * 0.98 + q * 0.02;
  }
  const double t = T6NowMs();
  if (last > 0) {
    const double d = t - last;
    sum += d;
    mx = std::max(mx, d);
    over175 += d > 17.5;
    over20 += d > 20.0;
    if (++n == 600) {
      REXLOG_INFO("T6 practice frames ({}): 600 periods avg {:.2f} ms, max {:.1f} ms, over 17.5 ms {}, over 20 ms {}, sound queue {:.1f}",
                  on ? "own 60/s clock" : "sound paced", sum / n, mx, over175, over20, depth_avg);
      n = over175 = over20 = 0;
      sum = mx = 0;
    }
  }
  last = t;
}

// T6_DUMP_AT_BATTLE=<tag>: XEX data/bss + game heaps at the first frame boundary with tcb_gamemode_battle2d_t
// -> runs/battle_<tag>_data.bin / _heap.bin (diff of two selections: where the game keeps characters / stage)
void T6BattleDump() {
  static const char* tag = T6Env("T6_DUMP_AT_BATTLE");
  static bool done = false;
  if (!tag || done || !g_t6_membase || !TaskTreeHas(0x8200C5E8u, 0x8200C5E8u)) return;
  done = true;
  for (const auto& [lo, hi, nm] : {std::tuple{0x82726000u, 0x82DB0000u, "data"}, std::tuple{0xA7290000u, 0xABA90000u, "heap"}}) {
    if (FILE* f = T6Open(T6DumpPath(fmt::format("battle_{}_{}.bin", tag, nm)), "wb")) {
      std::fwrite(g_t6_membase + lo, 1, hi - lo, f);
      std::fclose(f);
    }
  }
  REXLOG_INFO("T6 netplay: battle dump '{}' at step {}", tag, state().step);
}

bool T6NetRunning() { return g_net_running; }
bool T6NetResim() { return g_net_resim; }
uint32_t T6NetFrameNo() { return g_net_frame; }
uint64_t T6NetLoads() { return g_net_loads; }

// XInputGetState(user r3, XINPUT_STATE* r4) of the game (overrides the weak generated symbol): users 0 / 1 = players
// 1 / 2 get the session inputs of the frame being simulated (live or re-simulated)
double g_pad_host_ms = 0, g_local_in_ms = 0;
uint64_t g_pad_host_n = 0;
extern "C" double t6_pad_step_ms = 0;  // t6_watch.cpp: time in the game's pad step
