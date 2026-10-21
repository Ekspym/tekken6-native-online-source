#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cstring>

#include "t6_common.h"
#include <rex/rex_app.h>
#include <rex/ui/overlay/achievement_notification.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/command_processor.h>
#include <rex/ppc/func.h>
#include <rex/memory/utils.h>
#include <rex/filesystem/devices/host_path_device.h>

#include <atomic>
#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

extern std::atomic<int> g_t6_fa_value[2];      // t6_frame_data.cpp, [side]: frame advantage of the side's last hit, from its side
extern std::atomic<uint32_t> g_t6_fa_tick[2];  // GetTickCount of it, 0 = none yet
extern std::atomic<int> g_t6_fa_startup[2];    // startup of player 1's last attack (frame it became active)
extern std::atomic<uint32_t> g_t6_fa_startup_tick[2];
extern std::atomic<uint32_t> g_t6_pr_flags;      // t6_practice.cpp: 1 recording, 2 playing, 4 block after hit
extern std::atomic<uint32_t> g_t6_pr_frames;     // frames of the recording
extern std::atomic<uint32_t> g_t6_pr_msg_tick;   // GetTickCount of the last practice message
extern std::mutex g_t6_pr_msg_mutex;
extern std::string g_t6_pr_msg;
extern std::vector<std::string> g_t6_pr_lines;  // the practice menu's rows / the quick help (under g_t6_pr_msg_mutex)
extern std::atomic<int> g_t6_pr_cursor;         // the menu's selected row, -1 = closed
extern std::atomic<uint32_t> g_t6_fa_flag[2];  // player 2's reaction flag at the measurement (0x10000 = hit, all ones = whiff)
extern std::atomic<uint32_t> g_t6_fa_level[2];   // attack level word of that attack (move struct +0x10)
extern std::atomic<uint32_t> g_t6_fa_active[2];  // its active frames: first << 16 | last
extern std::atomic<int> g_t6_fa_damage[2];
void T6DrawFrameData(ImGuiIO& io, uint32_t now);    // t6_practice_hud.cpp
void T6DrawInputDisplay(ImGuiIO& io);

// Practice: the frame advantage of the last attack that connected, under the health bars (T6_FRAME_DATA=1).
class T6FrameDataDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit T6FrameDataDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override;
};

class Tekken6App final : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  ~Tekken6App() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx);

 protected:
  static bool StorageBootstrap();

  // (user 2026-10-13: not "tekken6 [rexglue-v0.10.0.0-dev.unknown-Release]")
  std::string OnWindowTitle() override { return "Tekken 6 Online"; }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;

  // No achievement toasts (input latency, 2026-10-17): the SDK's always registered toast dialog made the presenter
  // paint from the UI thread at its vblank tick - a finished frame waited 7 ms on average, up to 18 (T6_PRESENT_STATS,
  // pst1); without an ImGui dialog the guest output thread presents at once. T6_ACHIEVEMENT_TOASTS=1 = the SDK's.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override;

  void OnPreSetup(rex::RuntimeConfig& config) override;

  void OnPostLoadXexImage() override;

  void OnPostLaunchModule(rex::system::XThread* thread) override;

  void OnWindowFocusChanged(bool focused) override;

 private:
  // the guest output as a binary PPM (RGB of the RGBA rows); false when the file cannot be written
  static bool WritePpm(const std::filesystem::path& path, const rex::ui::RawImage& image);

  // the generated function a probe replaced, called with the probe's context
  static void CallOriginal(uint32_t address, PPCContext& ctx, uint8_t* base);

  // the readiness producer's words +116 / +284 / +288 / +292 / +296 (startup gate diagnostics)
  static std::string ProducerWords(uint32_t object);

  template <uint32_t Address>
  static std::atomic<uint32_t>& GuestProbeCount() {
    static std::atomic<uint32_t> count{0};
    return count;
  }

  static std::unordered_map<uint32_t, ::PPCFunc*>& GuestProbeOriginals();

  template <uint32_t Address>
  static void GuestProbe(PPCContext& ctx, uint8_t* base) {
    const uint32_t call = GuestProbeCount<Address>().fetch_add(1);
    const uint32_t probe_object = ctx.r3.u32;
    const uint32_t gate_before = ReadStartupGate();
    const uint32_t indirect_before = ctx.last_indirect_target;
    if (call < 16) {
      REXLOG_INFO(
          "Guest probe {:08X} #{} enter lr={:08X} r3={:08X} r4={:08X} r5={:08X} "
          "gate={:08X} last_indirect={:08X}",
          Address, call, static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, gate_before,
          indirect_before);
    }
    // the readiness producers: their object's words before and after the call (first 32 calls)
    constexpr bool kProducer = Address == 0x8221B480 || Address == 0x82219B70;
    const bool producer_log = kProducer && call < 32 && readiness_runtime_ && probe_object >= 0x80000000u && probe_object < 0xC0000000u;
    if (producer_log) REXLOG_INFO("Guest producer {:08X} #{} before object={:08X} {}", Address, call, probe_object, ProducerWords(probe_object));
    if constexpr (Address == 0x82218410) {
      if (call < 16 && readiness_runtime_ && probe_object >= 0x80000000u &&
          probe_object < 0xC0000000u) {
        const uint32_t list = ReadGuestWord(probe_object + 116);
        REXLOG_INFO("Guest poll-list {:08X} #{} object={:08X} +96={:08X} +116={:08X}", Address, call, probe_object,
                    ReadGuestWord(probe_object + 96), list);
        if (list >= 0x80000000u && list < 0xC0000000u) {
          REXLOG_INFO("Guest poll-list {:08X} #{} node={:08X} vtbl={:08X} next={:08X}", Address, call, list, ReadGuestWord(list + 0),
                      ReadGuestWord(list + 12));
        }
      }
    }
    CallOriginal(Address, ctx, base);
    const uint32_t gate_after = ReadStartupGate();
    if (call < 16 || gate_before != gate_after) {
      REXLOG_INFO(
          "Guest probe {:08X} #{} return lr={:08X} r3={:08X} gate={:08X} "
          "gate_delta={} last_indirect={:08X}",
          Address, call, static_cast<uint32_t>(ctx.lr), ctx.r3.u32, gate_after,
          gate_before == gate_after ? 0 : 1, ctx.last_indirect_target);
    }
    if (producer_log)
      REXLOG_INFO("Guest producer {:08X} #{} after object={:08X} r3={:08X} {}", Address, call, probe_object, ctx.r3.u32, ProducerWords(probe_object));
  }

  template <uint32_t Address>
  static void MovieProbe(PPCContext& ctx, uint8_t* base) {
    static std::atomic<uint64_t> call_count{0};
    static std::atomic<int64_t> last_log_ms{0};
    const uint64_t call = call_count.fetch_add(1, std::memory_order_relaxed) + 1;
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    int64_t last = last_log_ms.load(std::memory_order_relaxed);
    const bool log = call <= 16 || now_ms - last >= 1000;
    if (log && call > 16) {
      last_log_ms.compare_exchange_strong(last, now_ms, std::memory_order_relaxed);
    }
    const uint32_t before_r3 = ctx.r3.u32;
    const uint32_t before_r4 = ctx.r4.u32;
    const uint32_t before_r5 = ctx.r5.u32;
    const uint32_t before_r6 = ctx.r6.u32;
    const uint32_t before_r7 = ctx.r7.u32;
    const uint32_t before_r8 = ctx.r8.u32;
    const uint32_t before_r9 = ctx.r9.u32;
    const uint32_t before_r10 = ctx.r10.u32;
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);
    const auto started = std::chrono::steady_clock::now();
    CallOriginal(Address, ctx, base);
    if (log) {
      const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
      REXLOG_INFO(
          "T6 movie probe {:08X} call={} lr={:08X} args=({:08X},{:08X},{:08X},{:08X},"
          "{:08X},{:08X},{:08X},{:08X}) result=({:08X},{:08X},{:08X}) duration_us={}",
          Address, call, lr, before_r3, before_r4, before_r5, before_r6, before_r7,
          before_r8, before_r9, before_r10, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
          duration_us);
    }
  }

  static uint32_t ReadStartupGate() { return ReadGuestWord(0x8297F5DC); }

  template <uint32_t Address, uint32_t SlotOffset,
            uint32_t VtableRegister = 12, uint32_t MethodRegister = 11>
  void InstallVirtualCallThunk() {
    if (!runtime() || !runtime()->function_dispatcher()) return;
    if (!runtime()->function_dispatcher()->SetFunction(
            Address,
            &VirtualCallThunk<SlotOffset, VtableRegister, MethodRegister>)) {
      REXLOG_WARN("Tekken6 could not register vtable thunk {:08X} (slot +{:X})",
                  Address, SlotOffset);
    }
  }

  template <uint32_t Register>
  static void SetGpr(PPCContext& ctx, uint32_t value) {
    if constexpr (Register == 0) ctx.r0.u64 = value;
    else if constexpr (Register == 1) ctx.r1.u64 = value;
    else if constexpr (Register == 2) ctx.r2.u64 = value;
    else if constexpr (Register == 3) ctx.r3.u64 = value;
    else if constexpr (Register == 4) ctx.r4.u64 = value;
    else if constexpr (Register == 5) ctx.r5.u64 = value;
    else if constexpr (Register == 6) ctx.r6.u64 = value;
    else if constexpr (Register == 7) ctx.r7.u64 = value;
    else if constexpr (Register == 8) ctx.r8.u64 = value;
    else if constexpr (Register == 9) ctx.r9.u64 = value;
    else if constexpr (Register == 10) ctx.r10.u64 = value;
    else if constexpr (Register == 11) ctx.r11.u64 = value;
    else if constexpr (Register == 12) ctx.r12.u64 = value;
    else if constexpr (Register == 13) ctx.r13.u64 = value;
    else if constexpr (Register == 14) ctx.r14.u64 = value;
    else if constexpr (Register == 15) ctx.r15.u64 = value;
    else if constexpr (Register == 16) ctx.r16.u64 = value;
    else if constexpr (Register == 17) ctx.r17.u64 = value;
    else if constexpr (Register == 18) ctx.r18.u64 = value;
    else if constexpr (Register == 19) ctx.r19.u64 = value;
    else if constexpr (Register == 20) ctx.r20.u64 = value;
    else if constexpr (Register == 21) ctx.r21.u64 = value;
    else if constexpr (Register == 22) ctx.r22.u64 = value;
    else if constexpr (Register == 23) ctx.r23.u64 = value;
    else if constexpr (Register == 24) ctx.r24.u64 = value;
    else if constexpr (Register == 25) ctx.r25.u64 = value;
    else if constexpr (Register == 26) ctx.r26.u64 = value;
    else if constexpr (Register == 27) ctx.r27.u64 = value;
    else if constexpr (Register == 28) ctx.r28.u64 = value;
    else if constexpr (Register == 29) ctx.r29.u64 = value;
    else if constexpr (Register == 30) ctx.r30.u64 = value;
    else if constexpr (Register == 31) ctx.r31.u64 = value;
  }

  template <uint32_t SlotOffset, uint32_t VtableRegister,
            uint32_t MethodRegister>
  static void VirtualCallThunk(PPCContext& ctx, uint8_t* base) {
    const uint32_t vtable = rex::memory::load_and_swap<uint32_t>(
        base + ctx.r3.u32);
    const uint32_t slot_address = vtable + SlotOffset;
    const uint32_t method = rex::memory::load_and_swap<uint32_t>(
        base + slot_address);
    SetGpr<VtableRegister>(ctx, vtable);
    SetGpr<MethodRegister>(ctx, method);
    ctx.ctr.u64 = method;
    ctx.last_indirect_target = method;
    rex::runtime::ResolveIndirectFunction(method)(ctx, base);
  }

  static uint32_t ReadGuestWord(uint32_t address);

  template <uint32_t Address>
  void InstallGuestProbe() {
    if (!readiness_runtime_ || !readiness_runtime_->function_dispatcher()) {
      return;
    }
    auto* dispatcher = readiness_runtime_->function_dispatcher();
    auto* original = dispatcher->GetFunction(Address);
    if (!original) {
      REXLOG_WARN("Guest probe {:08X}: function is not registered", Address);
      return;
    }
    GuestProbeOriginals()[Address] = original;
    if (dispatcher->SetFunction(Address, &GuestProbe<Address>)) {
      REXLOG_INFO("Guest probe installed at {:08X}", Address);
    }
  }

  template <uint32_t Address>
  void InstallMovieProbe() {
    if (!runtime() || !runtime()->function_dispatcher()) return;
    auto* dispatcher = runtime()->function_dispatcher();
    auto* original = dispatcher->GetFunction(Address);
    if (!original) {
      REXLOG_WARN("T6 movie probe {:08X}: function is not registered in this build", Address);
      return;
    }
    GuestProbeOriginals()[Address] = original;
    if (dispatcher->SetFunction(Address, &MovieProbe<Address>)) {
      REXLOG_INFO("T6 movie probe installed at {:08X}", Address);
    }
  }

  static void ReadinessProbe(PPCContext& ctx, uint8_t* base);

  inline static rex::Runtime* readiness_runtime_ = nullptr;
  inline static ::PPCFunc* readiness_original_ = nullptr;
  inline static std::atomic<uint32_t> readiness_calls_{0};

  std::atomic_bool capture_stop_{false};
  std::thread capture_thread_;
  std::unique_ptr<rex::ui::ImGuiDrawer> frame_data_drawer_;  // (declared first: destroyed after its dialog)
  std::unique_ptr<T6FrameDataDialog> frame_data_dialog_;
};
