// Tekken 6 netplay - declarations shared by the t6_*.cpp modules (split of t6_netplay.cpp)
#pragma once

#include <intrin.h>
#include <ggponet.h>
#include "t6_common.h"
#include <rex/cvar.h>
#include <rex/system/xthread.h>
#include <rex/chrono/clock.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <map>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include <algorithm>
#include <tuple>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

void T6KeepMoney(uint8_t* base);  // t6_watch.cpp (T6_MONEY)
void T6WatchSave(uint8_t* base);  // t6_watch.cpp (T6_WATCH_SAVE)
void T6CustomizeTick(uint8_t* base, bool task);  // t6_watch.cpp (T6_CUSTOM_OUT)
extern "C" void __imp__sub_826A5ED0(PPCContext& ctx, uint8_t* base);  // CRT: per-thread data of the current thread
extern "C" void __imp__sub_82285FC0(PPCContext& ctx, uint8_t* base);  // fight: requirements of a cancel (cancel, player, opponent)
void T6CnuLockstep(bool on);                    // t6_audio_step.cpp: CnuSound tick thread in step with the frames
bool T6CnuAdvance(uint32_t ms, int timeout_ms);
extern int g_ggpo_save_confirmed;  // ggpo sync.cpp: inside a rollback, the last frame with every input known (else -1)
extern int g_ggpo_late_save;       // ggpo sync.cpp: save of a new frame after the poll, not at all when it rolled back
extern int g_ggpo_predicted;       // ggpo sync.cpp: the last input sync predicted an input
extern int g_ggpo_local_late, g_ggpo_remote_late;  // ggpo sync.cpp: share of predicted live frames (this side / the peer)
#ifdef T6_PGO_GEN
extern "C" int __llvm_profile_write_file(void);
#endif
void T6SampleStart();
void T6SampleDump(uint32_t frame);
bool T6NetResim();
void T6AudioKick(uint64_t f);  // t6_audio_step.cpp: audio step of the next frame started while this one is drawn
void T6AudioJoin();
bool T6StatusTakePark();  // t6_status.cpp: 'park' command of the launcher
void T6StatusPoll();      // t6_status.cpp: reads the launcher's command file now
void T6StatusReset();     // t6_status.cpp
void T6SndReset();        // t6_sound_boundary.cpp: sound boundary records

// ---- shared between the modules (global scope)
void T6SpikeFrameEnd();  // t6_resim.cpp
void AudioStepOrJoin(uint64_t f);  // t6_audio_step.cpp
void T6RandLog(uint32_t frame);  // t6_determinism.cpp
void T6VoiceStatsLog(uint32_t frame);  // t6_voices.cpp
void AudioStep(uint64_t f, bool in_session);  // t6_audio_step.cpp
void FightPlayerDump();  // t6_dumps.cpp
void FrameAdvantageTick();  // t6_frame_data.cpp
void PracticeTick();  // t6_practice.cpp
// tcb_kkMob* = the stage crowd (vtables from the RTTI)
inline constexpr uint32_t kT6CrowdVt[] = {0x82003408u, 0x82003494u, 0x8200353Cu, 0x8200366Cu, 0x820036ECu, 0x8200378Cu, 0x820037ECu,
                                          0x8200386Cu, 0x82003918u, 0x82003984u, 0x82003A5Cu, 0x82003ADCu, 0x82003B7Cu, 0x82003C10u};
std::vector<uint32_t> T6CrowdNodes();  // t6_resim.cpp
void T6CrowdPagesFrame();  // t6_resim.cpp
void T6CrowdClockTick();  // t6_resim.cpp
void T6PoseDiagDraw(bool begin);  // t6_pose_diag.cpp
void T6PoseDiagFrame();  // t6_pose_diag.cpp
void T6PoseDiagSave(uint32_t frame);  // t6_pose_diag.cpp
void PracticePadUser0(uint8_t* gamepad, bool connected);  // t6_practice.cpp
void PracticeEntries(uint8_t* p1, uint8_t* p2);  // t6_practice.cpp
bool PracticeDummyStart();  // t6_practice.cpp: pad 2 presses START (the CONTROLLER prompt)
bool PracticeDummyPad();  // t6_practice.cpp: the dummy's pad is connected (recording / playback)
bool PracticeTakeRebattle();  // t6_practice.cpp: the practice menu asked for a new battle (once)
void PracticeNewBattle();  // t6_practice.cpp: a (new) practice battle exists
void PracticeRawPad(uint16_t buttons);  // t6_practice.cpp: user 0's buttons before the button layout, + the keyboard
void ParkSession(uint32_t wait_steps);  // t6_audio_step.cpp
void PracticePace();
void StanceAudit();  // t6_frame_data.cpp
void T6BattleDump();
uint32_t T6MainPtd();  // t6_netplay.cpp
void T6NamePlatesFrame(uint8_t* base);  // t6_watch.cpp
bool T6NetFrame();
void T6NetHold();
bool T6NetRunning();
void T6OrphansTick();
uint32_t T6SimFrame();  // t6_sound_boundary.cpp
void T6SndReplayDeviceUpdate(uint8_t* base);
void T6SndStatsLog();
void T6StatusMatchInfo(uint32_t* addr);  // t6_status.cpp: match info object (sub_8238E3B8)
bool T6StatusTakeRematch();   // t6_status.cpp: 'rematch' command of the launcher
void T6VoicesAfterLoad();
void T6VoicesBeforeLoad();
extern double g_audio_parts[3];  // t6_netplay.cpp
extern uint32_t g_behind_n;  // t6_audio_step.cpp
extern uint32_t g_behind_sum;  // t6_audio_step.cpp
extern double g_bp[8];  // t6_audio_step.cpp
extern double g_bp_max[8];  // t6_audio_step.cpp
extern double g_bp_sum[6];  // t6_audio_step.cpp
extern double g_clock_audio_frames;  // t6_audio_step.cpp
extern double g_clock_push_ms;  // t6_audio_step.cpp
extern double g_clock_pushed_sum;  // t6_audio_step.cpp
extern double g_depth_sum;  // t6_audio_step.cpp
extern double g_depth_target;  // t6_audio_step.cpp
extern double g_draw_ms[3];
extern uint32_t g_fight_frames;  // t6_audio_step.cpp
extern uint32_t g_file_queues[4];  // t6_net_frame.cpp
extern uint64_t g_file_requests;  // t6_net_frame.cpp
extern double g_ft_bound_max;  // t6_audio_step.cpp
extern int g_ft_bound_over8;  // t6_audio_step.cpp
extern double g_ft_bound_sum;  // t6_audio_step.cpp
extern double g_ft_game_sum;  // t6_audio_step.cpp
extern double g_ft_last;  // t6_audio_step.cpp
extern int g_ft_late;  // t6_audio_step.cpp
extern double g_ft_out;  // t6_audio_step.cpp
extern int g_ft_over17;  // t6_audio_step.cpp
extern int g_ft_over25;  // t6_audio_step.cpp
extern double g_ft_period_max;  // t6_audio_step.cpp
extern double g_ft_period_sum;  // t6_audio_step.cpp
extern double g_ft_slack_sum;  // t6_audio_step.cpp
extern double g_ft_sleep;  // t6_audio_step.cpp
extern uint32_t g_lag_pred;  // t6_audio_step.cpp
extern double g_last_waits;  // t6_audio_step.cpp
extern double g_local_in_ms;  // t6_session.cpp
extern PPCContext * g_main_ctx;  // t6_netplay.cpp
extern double g_pad_host_ms;  // t6_session.cpp
extern uint64_t g_pad_host_n;  // t6_session.cpp
extern double g_phase_audio_ms;  // t6_audio_step.cpp
extern double g_phase_shift_ms;  // t6_audio_step.cpp
extern uint32_t g_ping_sum;  // t6_audio_step.cpp
extern double g_rbp_load0;  // t6_netplay.cpp
extern uint32_t g_slow_frames;  // t6_audio_step.cpp
extern uint64_t g_snd_caps[2];
extern double g_snd_ms[3];
extern "C" unsigned char t6_pagemap[0x100000];
extern "C" unsigned char t6_dirty[0x100000];
extern "C" unsigned char t6_dirty_l1[0x4000];
extern "C" unsigned char t6_fine_changed;
extern "C" unsigned char t6_fine_page[0x100000];
extern "C" unsigned char t6_fine_heap[0x480000];
extern "C" unsigned char t6_fine_data[0x69000];
extern "C" unsigned char t6_fine_virt[0x400000];
extern "C" void t6_dirty_mark(uint32_t a);
extern "C" PPCContext* t6_frame_ctx;
extern "C" double t6_pad_step_ms;
extern uint16_t g_audit_in[2][3];
extern "C" void __imp__sub_82525280(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_826B58E8(PPCContext& ctx, uint8_t* base);

namespace t6np {
void T6PlaybackDiag(const char* where);  // t6_session.cpp: Noh Theater crash (input playback globals)

constexpr uint32_t kEntrySize = 0x4C;
constexpr uint32_t kP1Offset = 80;
constexpr uint32_t kP2Offset = 156;
constexpr uint32_t kPadLo = 0x828B5550;  // sub_82268A60: raw slots 0x828B5550 (4 x 0x80), pads 0x828B5750 + i * 0x160
constexpr uint32_t kPadHi = 0x828B6800;
constexpr uint32_t kRecSize = 2 * kEntrySize + (kPadHi - kPadLo);
constexpr int kNetInput = 16;  // XINPUT_GAMEPAD (12 bytes, guest byte order) + padding
// Back to the hold point without a process restart (user 2026-10-11: "hra se nema vypinat a zapinat u kazdeho
// matche"). After a match the session is closed and the game leaves its result menu through CHARACTER SELECT by
// scripted pad input (offline: users 0 / 1 in the XInputGetState override): down, down, A in the result menu, then both
// users confirm every 40 pad steps (character select, stage select). pk1 (script without a stop): battle task gone at
// +210 steps, the stage confirm = user 0's press at +480, player setup +512, new battle task +634 - the same on both
// instances. So the script stops after +462 and the game is parked at +470 = in the stage select, like at the first
// hold point; 'start' -> user 0 confirms -> setup with the new characters / stage -> battle task -> a new session.
// Trigger: 'park' in T6_NET_CMD (launcher) or T6_NET_AUTOPARK=<frames in the result menu> (tests). Only from the
// result menu (game mode 27); a match that ended by a disconnect in the fight still needs a restart.
// The result menu remembers its last choice (pk2: the second park's down, down, A hit REMATCH - the cursor stood on
// CHARACTER SELECT from the first park and wrapped). g_result_cursor follows what was confirmed last (0 REMATCH after
// boot / a rematch, 2 CHARACTER SELECT after a park); the script presses down (2 - cursor) mod 4 times. All script
// times are t = pad steps since the session was closed - 30 (room for a third down press).
// The way from the result menu back to the second hold point (stage select), in pad steps (user 2026-10-13: "zkrat
// ten navrat na vyber areny"; the old fixed schedule held at +500). Measured with T6_PARK_TRACE + the name plate log:
//   result menu: down x n, A = CHARACTER SELECT, a SECOND A is needed (the old A at +120 did nothing visible, the
//   pulse at +190 did: tasks gone 12 steps later) -> the battle task is gone 50 steps after that A = T1 (old: +240);
//   character select takes input at T1 + 43; the stage select took user 1's press at T1 + 250 and then user 0's
//   stage confirm at T1 + 262, and nothing at T1 + 230.
// Only the menu part is shorter now: presses 15 steps apart (the recording's own are 13-16 apart), user 0 presses A
// every 10 steps until the battle task is gone (T1 = +115 instead of +240, ~0.65 s). From T1 on the presses are the
// OLD ones, step for step (pulses every 40 steps per user, hold at T1 + 260): two tries to shorten that part too
// (both users confirming every 10 steps up to T1 + 90, hold at T1 + 240 / + 265) reached the hold point but the
// stage confirm did nothing and the game stood there for good (e2e 11:42, 11:50) - the stage select needs the old
// sequence, why is not known. T1 is the same step on both sides (the state of the game decides it).
constexpr uint32_t kParkHold = 260;

struct Range {
  uint32_t lo, hi;
};
const std::vector<Range>& T6CrowdKeep();  // t6_resim.cpp: the crowd objects' bytes that stay live through a load

// A saved range: its bytes are at buf[off, off + size) of the snapshot (one buffer per snapshot, reused: hundreds of
// small vectors reallocated every frame made a 36 MB save take ~120 ms, v2-v5)
struct SnapRegion {
  uint32_t addr;
  uint32_t size;
  size_t off;
};

// One copy per page (2026-10-11, user: "nemuzeme mit drazsi rollback nez emulator"; PS3 copies a page once): the
// image of a page is a 4 KB buffer the page table g_inc_img_cache points to. A save takes a fresh buffer for a
// logged page, copies memory into it and hands the OLD image buffer to the undo log (no copy of the old content); a
// load gives the logged buffers back to the table (newest log first) and copies them into memory once. Buffers come
// from a pool and go back to it when a log is dropped or a load replaces them.
struct IncLog {
  uint32_t frame = 0;
  std::vector<uint32_t> pages;   // guest page numbers
  std::vector<uint8_t*> bufs;    // per page: its image at the previous save (owned by the log)
  std::vector<uint8_t> ptd;      // CRT per-thread block of the main thread at this save
  uint32_t ptd_addr = 0;
};

struct PagePool {
  std::vector<uint8_t*> chunks, free;
  uint8_t* Get() {
    if (free.empty()) {
      uint8_t* c = static_cast<uint8_t*>(_aligned_malloc(256 * 0x1000, 0x1000));
      chunks.push_back(c);
      for (int i = 255; i >= 0; i--) free.push_back(c + static_cast<size_t>(i) * 0x1000);
    }
    uint8_t* b = free.back();
    free.pop_back();
    return b;
  }
  void Put(uint8_t* b) { free.push_back(b); }
  void Reset() {
    free.clear();
    for (uint8_t* c : chunks) for (int i = 255; i >= 0; i--) free.push_back(c + static_cast<size_t>(i) * 0x1000);
  }
};

struct NetConfig {
  bool enabled = false;
  int player = 1;
  uint16_t local_port = 7000;
  std::string remote_ip = "127.0.0.1";
  uint16_t remote_port = 7001;
  int delay = 1;
  bool random_input = false;
  bool skip_intro = false;    // 'skipintro=1' of the start line (rematch): the character intros are skipped for both
  bool step_input = false;    // test (input latency): both stand, player 1 holds forward from session frame 1000
  bool attack_input = false;  // test: player 1 punches all the time, player 2 stands (a match with a real winner)
  uint32_t seed = 12345;
  // "<address>[:<port>]"
  void SetRemote(const std::string& v) {
    const auto colon = v.find(':');
    remote_ip = v.substr(0, colon);
    if (colon != std::string::npos) remote_port = static_cast<uint16_t>(T6ToInt(v.c_str() + colon + 1));
  }
};

// The global critical region of the runtime, held across the copy of a save / the restore of a load: interrupt
// callbacks (the guest vblank callback sub_824D9B38: swap queue, swap flag) run guest code under it, and with the
// guest vblank at 1 ms one ran in the middle of a load and never returned - every thread that needed the lock then
// stopped (pc4 silent freeze, pc5 loader never idle, pc6 main thread in XNotifyGetNext). T6_NO_GLOBAL_LOCK=1 = off.
struct GlobalLockHold {
  void (*f)(int);
  GlobalLockHold() : f(T6Env("T6_NO_GLOBAL_LOCK") ? nullptr : T6Rex().global_lock) {
    if (f) f(1);
  }
  ~GlobalLockHold() noexcept {
    if (f) f(0);
  }
};

struct NetLockHold {
  bool held = false;
  uint32_t h = 0;
  explicit NetLockHold(bool for_save = false);
  ~NetLockHold();
};

struct State;
struct SnapTest;

// ---- shared state and helpers of the modules
extern std::atomic<HANDLE> g_prof_main;  // t6_session.cpp (T6_PROFILE)
void ProfileThread();  // t6_session.cpp
void CalibrateFromMap(std::vector<Range>& ranges, std::vector<Range>& exclude, bool with_exclude = true);  // t6_snap_calibrate.cpp
void SetLocalDelay(int d);  // t6_session.cpp
void LocalInput(uint8_t* out);  // t6_session.cpp
void LocalLatched(uint8_t* local);  // t6_session.cpp
void KeepDroppedInput(uint8_t* local);  // t6_session.cpp
void MergePresses(uint8_t* local, uint8_t b0, uint8_t b1);  // t6_session.cpp
void CalibrateAsyncStart();  // t6_snap_calibrate.cpp
void CalibrateAsyncTake();  // t6_snap_calibrate.cpp
void ClearWriteMaps();  // t6_snap_calibrate.cpp
uint32_t CrtPtd();  // t6_session.cpp
void CrtRandSync();  // t6_session.cpp
void FindFightPositions();  // t6_session.cpp
uint64_t Fnv(const uint8_t* m, uint32_t n);  // t6_net_state.cpp
uint64_t FnvNoHandles(const uint8_t* p);  // t6_net_state.cpp
std::vector<uint64_t> FullHashes();  // t6_snapshot.cpp
bool IncEnabled();  // t6_snap_store.cpp
uint8_t* IncImage(uint32_t q, bool create);  // t6_snap_store.cpp
bool IncInScope(uint32_t q);  // t6_snap_store.cpp
uint16_t KeyboardButtons();
bool LatchOn();  // t6_session.cpp
bool LoadHoldGpu();  // t6_snap_store.cpp
void LocalPoll();  // t6_session.cpp
void LogSwapQueue(const char* tag);  // t6_hang_watch.cpp
double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b);
bool NetOwnPace();  // t6_session.cpp
uint32_t NetWakeHandle();  // t6_snap_workers.cpp
bool PageReadable(uint32_t q);  // t6_snap_store.cpp
std::vector<Range> ParseRanges(const char* rs);  // t6_net_state.cpp
void RefreshExclusionsIncremental();  // t6_snap_calibrate.cpp
void RemapPad(uint8_t* g);  // t6_session.cpp
void ResetWriteTracking();  // t6_snap_calibrate.cpp
int64_t ShadowIndex(uint32_t page);  // t6_snap_calibrate.cpp
uint32_t ShadowPageAddr(uint32_t idx);  // t6_snap_calibrate.cpp
bool SnapNoPhys();  // t6_snap_store.cpp
std::vector<Range>& SnapRanges();  // t6_net_state.cpp
void StartHangWatch(const uint32_t* step);  // t6_hang_watch.cpp
bool StartSession();  // t6_session.cpp
void SyncInputs();  // t6_session.cpp
bool T6InGameMemory(uint32_t page);
bool T6InLibHeap(uint32_t a);  // t6_lib_heap.cpp
void T6LatMoveLog();  // t6_session.cpp
void T6ParallelFor(size_t n, void (*fn)(size_t));  // t6_snap_store.cpp
void TakeShadow();
bool TaskTreeHas(uint32_t vt_a, uint32_t vt_b);  // t6_session.cpp
std::vector<uint32_t> TaskTreeVtables();  // t6_session.cpp
bool WaitLoaderIdle(int max_ms);  // t6_session.cpp
bool WaitWorkersIdle(int max_ms);  // t6_snap_workers.cpp
extern bool g_cal_done;  // t6_snap_calibrate.cpp
extern std::vector<Range> g_cal_exclude;  // t6_snap_calibrate.cpp
extern std::vector<uint32_t> g_dirty_stat;  // t6_snap_store.cpp
extern std::mutex g_drain_mutex;  // t6_snap_workers.cpp
extern std::vector<uint32_t> g_drain_queues;  // t6_snap_workers.cpp
extern double g_drawidle_ms[3];  // t6_snapshot.cpp
extern uint8_t g_drop_acc[2];  // t6_session.cpp
extern std::map<uint32_t, std::vector<Range>> g_excl_by_page;
extern std::vector<Range> g_excl_extra;  // t6_snap_calibrate.cpp
extern bool g_frame_dropped;  // t6_session.cpp
extern int g_frames_ahead;  // t6_session.cpp
extern uint32_t g_ft_skipped;  // t6_session.cpp
extern GGPOSession * g_ggpo;  // t6_session.cpp
extern bool g_in_rollback;  // t6_snapshot.cpp
extern bool g_inc_have;  // t6_snap_store.cpp
extern std::vector<uint8_t*> g_inc_img_cache;  // t6_snap_store.cpp
extern std::vector<uint8_t> g_inc_in_log;  // t6_snap_store.cpp
extern uint32_t g_inc_last;  // t6_snap_store.cpp
extern uint64_t g_inc_live_pages;  // t6_snap_store.cpp
extern uint64_t g_inc_live_saves;  // t6_snap_store.cpp
extern std::deque<IncLog> g_inc_logs;  // t6_snap_store.cpp
extern std::vector<uint32_t> g_inc_marked;  // t6_snap_store.cpp
extern uint64_t g_inc_missed;  // t6_snap_store.cpp
extern uint64_t g_inc_pages;  // t6_snap_store.cpp
extern IncLog g_inc_pending;  // t6_snap_store.cpp
extern bool g_inc_pending_valid;  // t6_snap_store.cpp
extern uint64_t g_inc_rb_pages;  // t6_snap_store.cpp
extern uint64_t g_inc_rb_saves;  // t6_snap_store.cpp
extern std::vector<uint8_t> g_inc_readable;  // t6_snap_store.cpp
extern double g_inc_resize_ms;  // t6_snap_store.cpp
extern uint64_t g_inc_resizes;  // t6_snap_store.cpp
extern uint64_t g_inc_restored;  // t6_snap_store.cpp
extern std::vector<IncLog> g_inc_spare;  // t6_snap_store.cpp
extern uint64_t g_inc_sweep;  // t6_snap_store.cpp
extern uint8_t g_last_sent[2];  // t6_session.cpp
extern uint64_t g_load_held;  // t6_snap_store.cpp
extern double g_load_ms;  // t6_session.cpp
extern double g_loader_wait_ms;  // t6_session.cpp
extern std::atomic<int> g_main_phase;  // t6_net_state.cpp
extern uint32_t g_menu_frames;  // t6_session.cpp
extern bool g_moku_match;  // t6_session.cpp
extern int g_net_depth;  // t6_session.cpp
extern std::atomic<bool> g_net_fast_over;  // t6_net_state.cpp
extern uint32_t g_net_frame;  // t6_session.cpp
extern uint32_t g_rb_end;  // t6_session.cpp
extern uint64_t g_net_frames;  // t6_session.cpp
extern uint64_t g_net_loads;  // t6_session.cpp
extern GGPOPlayerHandle g_net_local;  // t6_session.cpp
extern int g_net_max_pred;  // t6_session.cpp
extern int g_net_mode;  // t6_session.cpp
extern bool g_net_pending;  // t6_session.cpp
extern GGPOPlayerHandle g_net_remote;  // t6_session.cpp
extern bool g_net_resim;  // t6_session.cpp
extern uint64_t g_net_resim_frames;  // t6_session.cpp
extern bool g_net_running;  // t6_session.cpp
extern bool g_net_skip_draw;  // t6_session.cpp
extern bool g_net_started;  // t6_session.cpp
extern bool g_net_valid;  // t6_session.cpp
extern uint64_t g_net_wait_ms;  // t6_session.cpp
extern std::atomic<const char*> g_net_where;  // t6_net_state.cpp
extern double g_note_ms;  // t6_session.cpp
extern bool g_park;  // t6_session.cpp
extern uint32_t g_park_confirm;  // t6_session.cpp
extern int g_park_downs;  // t6_session.cpp
extern bool g_park_held;  // t6_session.cpp
extern bool g_park_new_battle;  // t6_session.cpp
extern bool g_park_no_battle;  // t6_session.cpp
extern bool g_park_started;  // t6_session.cpp
extern uint32_t g_park_step0;  // t6_session.cpp
extern uint32_t g_park_t1;  // t6_session.cpp
extern bool g_park_wanted;  // t6_session.cpp
extern uint32_t g_pos_addr;  // t6_session.cpp
extern bool g_practice_battle;  // t6_session.cpp
extern std::vector<uint8_t> g_pre_pages;  // t6_snap_calibrate.cpp
extern double g_rb_max;  // t6_session.cpp
extern double g_rb_ms;  // t6_session.cpp
extern double g_rbp[5];  // t6_session.cpp
extern double g_rbsave[4];  // t6_snapshot.cpp
extern double g_resim_ms;  // t6_session.cpp
extern uint64_t g_resim_saves_skipped;  // t6_session.cpp
extern int g_result_cursor;  // t6_session.cpp
extern uint64_t g_save_bytes;  // t6_snapshot.cpp
extern double g_save_ms;  // t6_session.cpp
extern double g_save_parts[4];  // t6_snapshot.cpp
extern std::vector<uint8_t> g_shadow;  // t6_snap_calibrate.cpp
extern std::vector<uint8_t> g_shadow_valid;  // t6_snap_calibrate.cpp
extern uint32_t g_snd_cur_addr;  // t6_sound_boundary.cpp
extern uint32_t g_snd_cur_lr;  // t6_sound_boundary.cpp
extern double g_stats_t0;  // t6_session.cpp
extern std::atomic<bool> g_t6_holding;  // t6_net_state.cpp
extern std::atomic<bool> g_watch_pause;  // t6_net_state.cpp
const NetConfig& net_config();  // t6_session.cpp
NetConfig& net_config_mut();  // t6_session.cpp
SnapTest& snaptest();  // t6_snapshot.cpp
State& state();  // t6_net_state.cpp
extern uint8_t g_net_in[2][kNetInput];
extern PagePool g_page_pool;

struct State {
  FILE* rec = nullptr;
  FILE* play = nullptr;
  FILE* hash = nullptr;
  bool play_done = false;
  uint32_t step = 0;
  std::vector<Range> ranges{{0x82726AC8u, 0x82DB0000u}};  // XEX image after the code: .data/.bss

  State() {
    if (const char* p = T6Env("T6_REC"); p && *p) {
      rec = T6Open(p, "wb");
      REXLOG_INFO("T6 netplay: recording inputs to {} ({})", p, rec ? "ok" : "FAILED");
    }
    if (const char* p = T6Env("T6_PLAY"); p && *p) {
      play = T6Open(p, "rb");
      REXLOG_INFO("T6 netplay: replaying inputs from {} ({})", p, play ? "ok" : "FAILED");
    }
    if (const char* rs = T6Env("T6_HASH_RANGES"); rs && *rs) {
      ranges = ParseRanges(rs);
    }
    if (const char* p = T6Env("T6_HASH"); p && *p) {
      hash = T6Open(p, "w");
      REXLOG_INFO("T6 netplay: state hashes to {} ({} ranges)", p, ranges.size());
    }
  }

  std::vector<uint64_t> Hashes() const {
    std::vector<uint64_t> h;
    h.reserve(ranges.size());
    for (const auto& rg : ranges) h.push_back(Fnv(g_t6_membase + rg.lo, rg.hi - rg.lo));
    return h;
  }
};

// Committed, readable host pages of guest range [lo, hi)
template <typename Fn>
void ForEachCommitted(uint32_t lo, uint32_t hi, const Fn& fn) {
  uint64_t a = lo;
  while (a < hi) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(g_t6_membase + a, &mbi, sizeof(mbi))) break;
    const uint64_t region_end = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(mbi.BaseAddress) - g_t6_membase) +
                                mbi.RegionSize;
    const uint64_t end = std::min<uint64_t>(region_end, hi);
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_READONLY))) {
      fn(static_cast<uint32_t>(a), static_cast<uint32_t>(end - a));
    }
    a = end;
  }
}

struct SnapTest {
  uint32_t at = 0, frames = 0;
  int phase = 3;  // 0 waiting, 1 first pass, 2 second pass, 3 done/off
  uint32_t i = 0;
  std::vector<SnapRegion> snap;
  std::vector<uint8_t> buf;
  const uint8_t* Data(const SnapRegion& r) const { return buf.data() + r.off; }
  size_t bytes = 0;
  std::vector<uint8_t> saved_pages;  // per shadow page: part of this snapshot
  bool quiet = false;                // netplay ring slot: no per-save logs, exclusions refreshed every 30 saves
  std::vector<std::vector<uint8_t>> inputs;
  std::vector<std::vector<uint64_t>> first;
  std::vector<std::vector<uint64_t>> first_full;  // every 30th frame
  uint32_t full_mismatches = 0;
  uint32_t mismatches = 0;

  explicit SnapTest(bool ring_slot = false);

  void Save(uint32_t frame = 0);

  // Incremental save of frame f (see IncLog): pages written since the last save, plus a sweep of a few unwritten pages
  // (a store the dirty map missed shows as a difference: logged, the page is saved too)
  static double* IncTp();
  static IncLog IncNewLog();
  // The pages written since the last call: their image so far goes into the undo log of the save (once per save),
  // the image takes the memory content. The dirty map is cleared before the copy: a store of another thread during
  // the copy marks the page again.
  static const std::vector<uint32_t>& IncCopyDirty(IncLog& log);
  // First pass of a live save (user 2026-10-11: everything into one frame). After a live frame the draw worker needs
  // ~2.9 ms more for its D3D commands and the save waited for it. Now the pages written so far are copied while it
  // still works; IncSave, after the wait, only takes the pages written meanwhile (the worker's) into the same log.
  static void IncFirstPass();
  void IncSave(uint32_t frame, const LARGE_INTEGER& q2);

  // Incremental load to frame t: pages written since the last save back to their image, then the undo logs of the
  // frames after t (newest first); the image follows, so it is the state of t afterwards
  bool IncLoad(uint32_t t) const;

  // Byte ranges (merged at 256 B) where memory now differs from the snapshot -> log + file
  void DiffToFile(const char* path, uint32_t frame) const;

  // false: the state of that frame is not there (the game goes on from a wrong state - the caller ends the match)
  bool Load(uint32_t frame = 0) const;
};

}  // namespace t6np
using namespace t6np;
