// Tekken 6 - the ReXGlue app of the game: member functions of tekken6_app.h

#include "generated/default/tekken6_init.h"
#include "tekken6_app.h"

// the practice tools (user 2026-10-20: "napoveda ... ani tam nemusi byt ... otevrene menu ma byt dokonce vetsi ...
// jediny kdy se ma male menu objevit je pokud nekdo udela quick action a to zase kde to bylo puvodne"): the open menu
// big in the middle of the screen with the selected row marked; closed, nothing - only a quick action's message (2 s)
// top left, with a running recording / playback / block after hit
static void DrawPracticeStatus(ImGuiIO& io, uint32_t now) {
  const uint32_t flags = g_t6_pr_flags.load(), mt = g_t6_pr_msg_tick.load();
  const bool msg = mt && now - mt < 2000;
  const int cursor = g_t6_pr_cursor.load();
  const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;
  const ImVec4 grey(0.82f, 0.82f, 0.86f, 0.9f), white(1.0f, 1.0f, 1.0f, 1.0f), gold(1.0f, 0.85f, 0.3f, 1.0f);
  const float scale = io.DisplaySize.y / 720.0f;
  if (cursor >= 0) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("##t6practicemenu", nullptr, wf)) {
      ImGui::SetWindowFontScale(scale * 1.9f);
      ImGui::TextColored(gold, "PRACTICE MENU");
      ImGui::Spacing();
      std::lock_guard lock(g_t6_pr_msg_mutex);
      for (size_t k = 0; k < g_t6_pr_lines.size(); k++) {
        if (static_cast<int>(k) == cursor) ImGui::TextColored(gold, "> %s", g_t6_pr_lines[k].c_str());
        else ImGui::TextColored(grey, "  %s", g_t6_pr_lines[k].c_str());
      }
      if (msg) ImGui::TextColored(white, "%s", g_t6_pr_msg.c_str());
    }
    ImGui::End();
    return;
  }
  if (!msg && !(flags & 1u)) return;  // (a recording keeps its REC line: the 30 s run out, START stops it)
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.17f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.45f);
  if (ImGui::Begin("##t6practice", nullptr, wf)) {
    ImGui::SetWindowFontScale(scale * 1.3f);
    if (flags & 1u) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC  %.1f / 30 s  (START: stop)", g_t6_pr_frames.load() / 60.0f);
    if (flags & 2u) ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "PLAYBACK  %.1f s", g_t6_pr_frames.load() / 60.0f);
    if (flags & 4u) ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f), "BLOCK AFTER FIRST HIT");
    if (msg) {
      std::lock_guard lock(g_t6_pr_msg_mutex);
      ImGui::TextColored(white, "%s", g_t6_pr_msg.c_str());
    }
  }
  ImGui::End();
}

void T6FrameDataDialog::OnDraw(ImGuiIO& io) {
  const uint32_t now = GetTickCount();
  DrawPracticeStatus(io, now);
  if (T6Env("T6_PRACTICE")) T6DrawInputDisplay(io);
  if (T6Env("T6_FRAME_DATA")) T6DrawFrameData(io, now);
}

Tekken6App::~Tekken6App() {
  capture_stop_.store(true);
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  // (the frame data drawer goes with this object: the presenter must not paint it any more)
  if (frame_data_drawer_) {
    auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
    if (auto* presenter = graphics ? graphics->presenter() : nullptr) presenter->SetGuestOutputOverlay(nullptr);
  }
}

std::unique_ptr<rex::ui::WindowedApp> Tekken6App::Create(rex::ui::WindowedAppContext& ctx) {
  return std::unique_ptr<Tekken6App>(new Tekken6App(ctx, "tekken6",
                                                      PPCImageConfig));
}

bool Tekken6App::StorageBootstrap() {
  const char* v = T6Env("TEKKEN6_STORAGE_BOOTSTRAP");
  return v && *v == '1';
}

void Tekken6App::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  if ((!T6Env("T6_FRAME_DATA") && !T6Env("T6_PRACTICE")) || !drawer) return;  // (the practice tools' status too)
  // (input latency, 2026-10-17: a dialog of the app's ImGui drawer is a UI drawer - the presenter then paints from the
  // UI thread at its vblank tick, ~1 frame later. Own drawer without input, drawn by the guest output thread's
  // present as the presenter's guest output overlay - set in OnPostLaunchModule: the runtime and its presenter do
  // not exist yet here. T6_FRAME_DATA_UI=1 = the old UI drawer.)
  if (!window() || !immediate_drawer() || T6Env("T6_FRAME_DATA_UI")) {
    frame_data_dialog_ = std::make_unique<T6FrameDataDialog>(drawer);
    return;
  }
  frame_data_drawer_ = std::make_unique<rex::ui::ImGuiDrawer>(window(), 63);
  frame_data_drawer_->SetNoInput(true);
  frame_data_drawer_->SetImmediateDrawer(immediate_drawer());
  frame_data_dialog_ = std::make_unique<T6FrameDataDialog>(frame_data_drawer_.get());
}

std::unique_ptr<rex::ui::AchievementNotificationDialog> Tekken6App::CreateAchievementNotificationDialog() {
  if (T6Env("T6_ACHIEVEMENT_TOASTS")) return rex::ReXApp::CreateAchievementNotificationDialog();
  return nullptr;
}

void Tekken6App::OnPreSetup(rex::RuntimeConfig& config) {
  // ReXGlue 0.10 loads Xenos as a runtime plugin. Do not silently run with
  // the SDK's default no-GPU mode: that would test only guest bring-up.
  if (config.gpu_plugin.empty()) {
    config.gpu_plugin = "xenos";
  }
  // Tekken's Cache1 bootstrap checks sectors_per_allocation_unit *
  // bytes_per_sector == 0x8000.  Keep the SDK/Xenia default (0x10000) for
  // other titles and scope this compatibility geometry to Tekken.
  // Xenia (Tekken 6 "gameplay") keeps the default geometry: the utility-drive mount then fails with
  // STATUS_UNRECOGNIZED_VOLUME and the game loads from the disc without a cache. With 0x40 the mount succeeded,
  // the game used cache: (not provided) and its readiness poll never finished. TEKKEN6_STORAGE_BOOTSTRAP=1: old way.
  if (StorageBootstrap()) {
    config.null_device_sectors_per_allocation_unit = 0x40;
  }
  // Started without arguments (double click on tekken6.exe = offline play): the render target path the launcher
  // passes - the SDK's default ("" = rtv) draws nothing with this game (black screen, practice test 2026-10-12).
  if (rex::cvar::GetFlagByName("render_target_path_d3d12").empty()) rex::cvar::SetFlagByName("render_target_path_d3d12", "rov");
  REXLOG_INFO("Tekken6 RexGlue prototype: Xenos GPU plugin requested");
}

void Tekken6App::OnPostLoadXexImage() {
  if (runtime() && runtime()->memory()) {
    g_t6_membase = runtime()->memory()->virtual_membase();
  }
  static auto* rt = runtime();
  g_t6_sys_alloc = [](uint32_t size, uint32_t align) -> uint32_t {
    return rt && rt->memory() ? rt->memory()->SystemHeapAlloc(size, align) : 0u;
  };
  g_t6_sys_free = [](uint32_t addr) {
    if (rt && rt->memory()) rt->memory()->SystemHeapFree(addr);
  };
  g_t6_gpu_ptrs = []() -> uint64_t {
    auto* gs = rt ? dynamic_cast<rex::graphics::GraphicsSystem*>(rt->graphics_system()) : nullptr;
    auto* cp = gs ? gs->command_processor() : nullptr;
    return cp ? (uint64_t(cp->debug_read_ptr()) << 32) | cp->debug_write_ptr() : 0;
  };
  g_t6_gpu_idle = []() -> bool {
    auto* gs = rt ? dynamic_cast<rex::graphics::GraphicsSystem*>(rt->graphics_system()) : nullptr;
    auto* cp = gs ? gs->command_processor() : nullptr;
    return !cp || cp->is_ring_idle();
  };
  // These retail entries are vtable tail-call thunks that XenonRecomp did
  // not emit as function boundaries. Their verified four-instruction
  // sequences load the object's vtable slot into CTR and branch through it.
  // Install them before guest startup dispatches any of them.
  InstallVirtualCallThunk<0x82248968, 0x90>();
  InstallVirtualCallThunk<0x82248988, 0x12C>();
  InstallVirtualCallThunk<0x82248998, 0xB0>();
  InstallVirtualCallThunk<0x822489A8, 0xC0>();
  InstallVirtualCallThunk<0x822489B8, 0xE0>();
  InstallVirtualCallThunk<0x822489C8, 0x120>();
  InstallVirtualCallThunk<0x822489D8, 0xB4>();
  InstallVirtualCallThunk<0x821DA618, 0x130>();
  InstallVirtualCallThunk<0x822249A0, 0x104>();
  InstallVirtualCallThunk<0x822249B0, 0x114>();
  InstallVirtualCallThunk<0x822249C0, 0xE8>();
  InstallVirtualCallThunk<0x822249D0, 0x108>();
  InstallVirtualCallThunk<0x822249E0, 0x118>();
  InstallVirtualCallThunk<0x82224A08, 0xFC>();
  InstallVirtualCallThunk<0x82224A18, 0x10C>();
  InstallVirtualCallThunk<0x82224A28, 0xF0>();
  InstallVirtualCallThunk<0x82224A38, 0x100>();
  InstallVirtualCallThunk<0x82224A48, 0x110>();
  InstallVirtualCallThunk<0x822488A8, 0xF4>();
  InstallVirtualCallThunk<0x822488B8, 0x88>();
  InstallVirtualCallThunk<0x822488C8, 0x124>();
  InstallVirtualCallThunk<0x822488D8, 0xB8>();
  InstallVirtualCallThunk<0x822488E8, 0xC8>();
  InstallVirtualCallThunk<0x822488F8, 0xD8>();
  InstallVirtualCallThunk<0x82248908, 0xF8>();
  InstallVirtualCallThunk<0x82248918, 0x128>();
  InstallVirtualCallThunk<0x82248928, 0xBC>();
  InstallVirtualCallThunk<0x82248938, 0xCC>();
  InstallVirtualCallThunk<0x82248948, 0xDC>();
  InstallVirtualCallThunk<0x82248958, 0x80>();
  InstallVirtualCallThunk<0x82411178, 0x1A0>();
  InstallVirtualCallThunk<0x82411188, 0x1D0>();
  InstallVirtualCallThunk<0x82411198, 0x164>();
  InstallVirtualCallThunk<0x824111A8, 0x174>();
  InstallVirtualCallThunk<0x824111B8, 0x184>();
  InstallVirtualCallThunk<0x824111F8, 0x168>();
  InstallVirtualCallThunk<0x82411208, 0x188>();
  InstallVirtualCallThunk<0x82411218, 0x198>();
  InstallVirtualCallThunk<0x82411228, 0x1A8>();
  InstallVirtualCallThunk<0x82411238, 0x1D8>();
  InstallVirtualCallThunk<0x82411248, 0x15C>();
  InstallVirtualCallThunk<0x82411278, 0x17C>();
  InstallVirtualCallThunk<0x82411288, 0x18C>();
  InstallVirtualCallThunk<0x82411298, 0x19C>();
  InstallVirtualCallThunk<0x8253E4D0, 0x24, 11, 10>();

  // Additional exact-pattern tail-call thunks found in the full image scan.
  InstallVirtualCallThunk<0x82218538, 0x4, 7, 6>();
  InstallVirtualCallThunk<0x821EC970, 0x4, 11, 10>();
  InstallVirtualCallThunk<0x821ECAD4, 0x4, 11, 10>();
  InstallVirtualCallThunk<0x8220B2A8, 0x28, 11, 10>();
  InstallVirtualCallThunk<0x82248898, 0xE4>();
  InstallVirtualCallThunk<0x82248978, 0x11C>();
  InstallVirtualCallThunk<0x8224A0A4, 0x8, 11, 10>();
  InstallVirtualCallThunk<0x8234EED0, 0x60, 11, 10>();
  InstallVirtualCallThunk<0x82411138, 0x160>();
  InstallVirtualCallThunk<0x82411148, 0x278>();
  InstallVirtualCallThunk<0x82411158, 0x180>();
  InstallVirtualCallThunk<0x82411168, 0x190>();
  InstallVirtualCallThunk<0x824111C8, 0x194>();
  InstallVirtualCallThunk<0x824111D8, 0x1A4>();
  InstallVirtualCallThunk<0x824111E8, 0x1D4>();
  InstallVirtualCallThunk<0x82411258, 0x16C>();
  InstallVirtualCallThunk<0x82411268, 0x274>();
  InstallVirtualCallThunk<0x82459E80, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x82459E98, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x82459EB0, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x82459EC8, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x82459EE0, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x82459F30, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x82459F48, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x82459F60, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x82459F78, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x82459F90, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x8249D4C4, 0x28, 11, 10>();
  InstallVirtualCallThunk<0x824A8394, 0x38, 11, 10>();
  InstallVirtualCallThunk<0x824A83AC, 0x3C, 11, 10>();
  InstallVirtualCallThunk<0x824A8518, 0x34, 11, 10>();
  InstallVirtualCallThunk<0x824F1B70, 0x8, 11, 10>();
  InstallVirtualCallThunk<0x824F1BB0, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x824F3958, 0x4, 11, 10>();
  InstallVirtualCallThunk<0x82528060, 0xC, 11, 10>();
  InstallVirtualCallThunk<0x8256B22C, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8256BB28, 0x14, 11, 10>();
  InstallVirtualCallThunk<0x82592658, 0x88, 11, 10>();
  InstallVirtualCallThunk<0x825C52C0, 0x1C, 11, 11>();
  InstallVirtualCallThunk<0x8268B568, 0x14, 11, 11>();
  InstallVirtualCallThunk<0x8268B5B0, 0x18, 11, 11>();
  InstallVirtualCallThunk<0x8268B680, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x8268B6C8, 0x10, 11, 11>();
  InstallVirtualCallThunk<0x82698ED4, 0x10, 11, 11>();
  InstallVirtualCallThunk<0x8222A88C, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8222A8A4, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8226F964, 0x14, 11, 10>();
  InstallVirtualCallThunk<0x8233BE78, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x8233C3E8, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x8236F218, 0xB0, 11, 10>();
  InstallVirtualCallThunk<0x8238EB30, 0x6C, 11, 10>();
  InstallVirtualCallThunk<0x823E9574, 0x8, 11, 10>();
  InstallVirtualCallThunk<0x8242D7DC, 0x2BC, 11, 10>();
  InstallVirtualCallThunk<0x8242D7EC, 0x2C0, 11, 10>();
  InstallVirtualCallThunk<0x8242D7FC, 0x2C4, 11, 10>();
  InstallVirtualCallThunk<0x8242D80C, 0x2C8, 11, 10>();
  InstallVirtualCallThunk<0x8242D81C, 0x2CC, 11, 10>();
  InstallVirtualCallThunk<0x8242D82C, 0x2D0, 11, 10>();
  InstallVirtualCallThunk<0x8242D83C, 0x2D4, 11, 10>();
  InstallVirtualCallThunk<0x8242D84C, 0x2D8, 11, 10>();
  InstallVirtualCallThunk<0x8242D85C, 0x2DC, 11, 10>();
  InstallVirtualCallThunk<0x8242D86C, 0x2E0, 11, 10>();
  InstallVirtualCallThunk<0x8242D87C, 0x2E4, 11, 10>();
  InstallVirtualCallThunk<0x8242D88C, 0x2E8, 11, 10>();
  InstallVirtualCallThunk<0x8242D89C, 0x2EC, 11, 10>();
  InstallVirtualCallThunk<0x8242D8AC, 0x2F0, 11, 10>();
  InstallVirtualCallThunk<0x8242D8BC, 0x2F4, 11, 10>();
  InstallVirtualCallThunk<0x8242D8CC, 0x2F8, 11, 10>();
  InstallVirtualCallThunk<0x8242D8DC, 0x2FC, 11, 10>();
  InstallVirtualCallThunk<0x8242D8EC, 0x300, 11, 10>();
  InstallVirtualCallThunk<0x8242D8FC, 0x304, 11, 10>();
  InstallVirtualCallThunk<0x8242D90C, 0x308, 11, 10>();
  InstallVirtualCallThunk<0x8242D91C, 0x30C, 11, 10>();
  InstallVirtualCallThunk<0x8242D92C, 0x310, 11, 10>();
  InstallVirtualCallThunk<0x8242D93C, 0x314, 11, 10>();
  InstallVirtualCallThunk<0x8242D94C, 0x318, 11, 10>();
  InstallVirtualCallThunk<0x8242D95C, 0x31C, 11, 10>();
  InstallVirtualCallThunk<0x8242D96C, 0x320, 11, 10>();
  InstallVirtualCallThunk<0x8242D97C, 0x324, 11, 10>();
  InstallVirtualCallThunk<0x8242D98C, 0x328, 11, 10>();
  InstallVirtualCallThunk<0x8242D99C, 0x32C, 11, 10>();
  InstallVirtualCallThunk<0x8242D9AC, 0x330, 11, 10>();
  InstallVirtualCallThunk<0x8242D9BC, 0x334, 11, 10>();
  InstallVirtualCallThunk<0x8242D9CC, 0x338, 11, 10>();
  InstallVirtualCallThunk<0x8242D9DC, 0x33C, 11, 10>();
  InstallVirtualCallThunk<0x8242D9EC, 0x340, 11, 10>();
  InstallVirtualCallThunk<0x8242D9FC, 0x344, 11, 10>();
  InstallVirtualCallThunk<0x8242DA0C, 0x348, 11, 10>();
  InstallVirtualCallThunk<0x8242DA1C, 0x34C, 11, 10>();
  InstallVirtualCallThunk<0x8242DA2C, 0x350, 11, 10>();
  InstallVirtualCallThunk<0x8242DA3C, 0x354, 11, 10>();
  InstallVirtualCallThunk<0x8242DA4C, 0x358, 11, 10>();
  InstallVirtualCallThunk<0x8242DA5C, 0x35C, 11, 10>();
  InstallVirtualCallThunk<0x8242DA6C, 0x360, 11, 10>();
  InstallVirtualCallThunk<0x8242DA7C, 0x364, 11, 10>();
  InstallVirtualCallThunk<0x8242DA8C, 0x368, 11, 10>();
  InstallVirtualCallThunk<0x8242DA9C, 0x36C, 11, 10>();
  InstallVirtualCallThunk<0x8242DAAC, 0x370, 11, 10>();
  InstallVirtualCallThunk<0x8242DABC, 0x374, 11, 10>();
  InstallVirtualCallThunk<0x8242DACC, 0x378, 11, 10>();
  InstallVirtualCallThunk<0x8242DADC, 0x37C, 11, 10>();
  InstallVirtualCallThunk<0x8242DAEC, 0x380, 11, 10>();
  InstallVirtualCallThunk<0x8242DAFC, 0x384, 11, 10>();
  InstallVirtualCallThunk<0x8242DB0C, 0x388, 11, 10>();
  InstallVirtualCallThunk<0x8242DB1C, 0x38C, 11, 10>();
  InstallVirtualCallThunk<0x8242DB2C, 0x390, 11, 10>();
  InstallVirtualCallThunk<0x8242DB3C, 0x394, 11, 10>();
  InstallVirtualCallThunk<0x8242DB4C, 0x398, 11, 10>();
  InstallVirtualCallThunk<0x8242DB5C, 0x39C, 11, 10>();
  InstallVirtualCallThunk<0x8242DB6C, 0x3A0, 11, 10>();
  InstallVirtualCallThunk<0x8242DB7C, 0x3A4, 11, 10>();
  InstallVirtualCallThunk<0x8242DB8C, 0x3A8, 11, 10>();
  InstallVirtualCallThunk<0x8242DB9C, 0x3AC, 11, 10>();
  InstallVirtualCallThunk<0x8242DBAC, 0x3B0, 11, 10>();
  InstallVirtualCallThunk<0x8242DBBC, 0x3B4, 11, 10>();
  InstallVirtualCallThunk<0x8242DBCC, 0x3B8, 11, 10>();
  InstallVirtualCallThunk<0x8242DBDC, 0x3BC, 11, 10>();
  InstallVirtualCallThunk<0x8242DBEC, 0x3C0, 11, 10>();
  InstallVirtualCallThunk<0x8242DBFC, 0x3C4, 11, 10>();
  InstallVirtualCallThunk<0x8242DC0C, 0x3C8, 11, 10>();
  InstallVirtualCallThunk<0x8242DC1C, 0x3CC, 11, 10>();
  InstallVirtualCallThunk<0x8242DC2C, 0x3D0, 11, 10>();
  InstallVirtualCallThunk<0x8242DC3C, 0x3D4, 11, 10>();
  InstallVirtualCallThunk<0x8242DC4C, 0x3D8, 11, 10>();
  InstallVirtualCallThunk<0x8242DC5C, 0x3DC, 11, 10>();
  InstallVirtualCallThunk<0x8242DC6C, 0x3E0, 11, 10>();
  InstallVirtualCallThunk<0x8242DC7C, 0x3E4, 11, 10>();
  InstallVirtualCallThunk<0x8242DC8C, 0x3E8, 11, 10>();
  InstallVirtualCallThunk<0x8242DC9C, 0x3EC, 11, 10>();
  InstallVirtualCallThunk<0x8242DCAC, 0x3F0, 11, 10>();
  InstallVirtualCallThunk<0x8242DCBC, 0x3F4, 11, 10>();
  InstallVirtualCallThunk<0x8242DCCC, 0x3F8, 11, 10>();
  InstallVirtualCallThunk<0x8242DCDC, 0x3FC, 11, 10>();
  InstallVirtualCallThunk<0x8242DCEC, 0x400, 11, 10>();
  InstallVirtualCallThunk<0x8242DCFC, 0x404, 11, 10>();
  InstallVirtualCallThunk<0x8242DD0C, 0x408, 11, 10>();
  InstallVirtualCallThunk<0x8242DD1C, 0x40C, 11, 10>();
  InstallVirtualCallThunk<0x8242DD2C, 0x410, 11, 10>();
  InstallVirtualCallThunk<0x8242DD3C, 0x414, 11, 10>();
  InstallVirtualCallThunk<0x8242DD4C, 0x418, 11, 10>();
  InstallVirtualCallThunk<0x8242DD5C, 0x41C, 11, 10>();
  InstallVirtualCallThunk<0x8242DD6C, 0x420, 11, 10>();
  InstallVirtualCallThunk<0x8242DD7C, 0x424, 11, 10>();
  InstallVirtualCallThunk<0x8242DD8C, 0x428, 11, 10>();
  InstallVirtualCallThunk<0x8242DD9C, 0x42C, 11, 10>();
  InstallVirtualCallThunk<0x8242DDAC, 0x430, 11, 10>();
  InstallVirtualCallThunk<0x8242DDBC, 0x434, 11, 10>();
  InstallVirtualCallThunk<0x8242DDCC, 0x438, 11, 10>();
  InstallVirtualCallThunk<0x8242DDDC, 0x43C, 11, 10>();
  InstallVirtualCallThunk<0x8242DDEC, 0x440, 11, 10>();
  InstallVirtualCallThunk<0x8242DDFC, 0x444, 11, 10>();
  InstallVirtualCallThunk<0x8242DE0C, 0x448, 11, 10>();
  InstallVirtualCallThunk<0x8242DE1C, 0x44C, 11, 10>();
  InstallVirtualCallThunk<0x8242DE2C, 0x450, 11, 10>();
  InstallVirtualCallThunk<0x8242DE3C, 0x454, 11, 10>();
  InstallVirtualCallThunk<0x8242DE4C, 0x458, 11, 10>();
  InstallVirtualCallThunk<0x8242DE5C, 0x45C, 11, 10>();
  InstallVirtualCallThunk<0x8242DE6C, 0x460, 11, 10>();
  InstallVirtualCallThunk<0x8242DE7C, 0x464, 11, 10>();
  InstallVirtualCallThunk<0x8242DE8C, 0x468, 11, 10>();
  InstallVirtualCallThunk<0x8242DE9C, 0x46C, 11, 10>();
  InstallVirtualCallThunk<0x8242DEAC, 0x470, 11, 10>();
  InstallVirtualCallThunk<0x8242DEBC, 0x474, 11, 10>();
  InstallVirtualCallThunk<0x8242DECC, 0x478, 11, 10>();
  InstallVirtualCallThunk<0x8242DEDC, 0x47C, 11, 10>();
  InstallVirtualCallThunk<0x8242DEEC, 0x480, 11, 10>();
  InstallVirtualCallThunk<0x8242DEFC, 0x484, 11, 10>();
  InstallVirtualCallThunk<0x8242DF0C, 0x488, 11, 10>();
  InstallVirtualCallThunk<0x8242DF1C, 0x48C, 11, 10>();
  InstallVirtualCallThunk<0x8242DF2C, 0x490, 11, 10>();
  InstallVirtualCallThunk<0x8242DF3C, 0x494, 11, 10>();
  InstallVirtualCallThunk<0x8242DF4C, 0x498, 11, 10>();
  InstallVirtualCallThunk<0x8242DF5C, 0x49C, 11, 10>();
  InstallVirtualCallThunk<0x8242DF6C, 0x4A0, 11, 10>();
  InstallVirtualCallThunk<0x8242DF7C, 0x4A4, 11, 10>();
  InstallVirtualCallThunk<0x8242DF8C, 0x4A8, 11, 10>();
  InstallVirtualCallThunk<0x8242DF9C, 0x4AC, 11, 10>();
  InstallVirtualCallThunk<0x8242DFAC, 0x4B0, 11, 10>();
  InstallVirtualCallThunk<0x8242DFBC, 0x4B4, 11, 10>();
  InstallVirtualCallThunk<0x8242DFCC, 0x4B8, 11, 10>();
  InstallVirtualCallThunk<0x8242DFDC, 0x4BC, 11, 10>();
  InstallVirtualCallThunk<0x8242DFEC, 0x4C0, 11, 10>();
  InstallVirtualCallThunk<0x8242DFFC, 0x4C4, 11, 10>();
  InstallVirtualCallThunk<0x8242E00C, 0x4C8, 11, 10>();
  InstallVirtualCallThunk<0x8242E01C, 0x4CC, 11, 10>();
  InstallVirtualCallThunk<0x8242E02C, 0x4D0, 11, 10>();
  InstallVirtualCallThunk<0x8242E03C, 0x4D4, 11, 10>();
  InstallVirtualCallThunk<0x8242E04C, 0x4D8, 11, 10>();
  InstallVirtualCallThunk<0x8242E05C, 0x4DC, 11, 10>();
  InstallVirtualCallThunk<0x8242E06C, 0x4E0, 11, 10>();
  InstallVirtualCallThunk<0x8242E07C, 0x4E4, 11, 10>();
  InstallVirtualCallThunk<0x8242E08C, 0x4E8, 11, 10>();
  InstallVirtualCallThunk<0x8242E09C, 0x4EC, 11, 10>();
  InstallVirtualCallThunk<0x8242E0AC, 0x4F0, 11, 10>();
  InstallVirtualCallThunk<0x8242E0BC, 0x4F4, 11, 10>();
  InstallVirtualCallThunk<0x8242E0CC, 0x4F8, 11, 10>();
  InstallVirtualCallThunk<0x8242E0DC, 0x4FC, 11, 10>();
  InstallVirtualCallThunk<0x8242E0EC, 0x500, 11, 10>();
  InstallVirtualCallThunk<0x8242E0FC, 0x504, 11, 10>();
  InstallVirtualCallThunk<0x8242E10C, 0x508, 11, 10>();
  InstallVirtualCallThunk<0x8242E11C, 0x50C, 11, 10>();
  InstallVirtualCallThunk<0x8242E12C, 0x510, 11, 10>();
  InstallVirtualCallThunk<0x8242E13C, 0x514, 11, 10>();
  InstallVirtualCallThunk<0x8242E14C, 0x518, 11, 10>();
  InstallVirtualCallThunk<0x8242E15C, 0x51C, 11, 10>();
  InstallVirtualCallThunk<0x8242E16C, 0x520, 11, 10>();
  InstallVirtualCallThunk<0x8242E17C, 0x524, 11, 10>();
  InstallVirtualCallThunk<0x8242E18C, 0x528, 11, 10>();
  InstallVirtualCallThunk<0x8242E19C, 0x52C, 11, 10>();
  InstallVirtualCallThunk<0x8242E1AC, 0x530, 11, 10>();
  InstallVirtualCallThunk<0x8242E1BC, 0x534, 11, 10>();
  InstallVirtualCallThunk<0x8242E1CC, 0x538, 11, 10>();
  InstallVirtualCallThunk<0x8242E1DC, 0x53C, 11, 10>();
  InstallVirtualCallThunk<0x8242E1EC, 0x540, 11, 10>();
  InstallVirtualCallThunk<0x8242E1FC, 0x544, 11, 10>();
  InstallVirtualCallThunk<0x8242E20C, 0x548, 11, 10>();
  InstallVirtualCallThunk<0x8242E21C, 0x54C, 11, 10>();
  InstallVirtualCallThunk<0x8242E22C, 0x550, 11, 10>();
  InstallVirtualCallThunk<0x8242E23C, 0x554, 11, 10>();
  InstallVirtualCallThunk<0x8242E24C, 0x558, 11, 10>();
  InstallVirtualCallThunk<0x8242E25C, 0x55C, 11, 10>();
  InstallVirtualCallThunk<0x8242E26C, 0x560, 11, 10>();
  InstallVirtualCallThunk<0x8242E27C, 0x564, 11, 10>();
  InstallVirtualCallThunk<0x8242E28C, 0x568, 11, 10>();
  InstallVirtualCallThunk<0x8242E29C, 0x56C, 11, 10>();
  InstallVirtualCallThunk<0x8242E2AC, 0x570, 11, 10>();
  InstallVirtualCallThunk<0x8242E2BC, 0x574, 11, 10>();
  InstallVirtualCallThunk<0x824F1B90, 0x14, 11, 10>();
  InstallVirtualCallThunk<0x824F55E0, 0xC, 11, 10>();
  InstallVirtualCallThunk<0x824F5628, 0x10, 11, 10>();
  InstallVirtualCallThunk<0x824FADE0, 0x34, 11, 10>();
  InstallVirtualCallThunk<0x824FAE04, 0x34, 11, 10>();
  InstallVirtualCallThunk<0x824FAE30, 0x3C, 11, 10>();
  InstallVirtualCallThunk<0x824FAE5C, 0x44, 11, 10>();
  InstallVirtualCallThunk<0x824FAE88, 0x4C, 11, 10>();
  InstallVirtualCallThunk<0x824FAED8, 0x38, 11, 10>();
  InstallVirtualCallThunk<0x824FAF28, 0x40, 11, 10>();
  InstallVirtualCallThunk<0x824FAF54, 0x48, 11, 10>();
  InstallVirtualCallThunk<0x824FAF80, 0x50, 11, 10>();
  InstallVirtualCallThunk<0x8254DB84, 0x10, 11, 10>();
  InstallVirtualCallThunk<0x8254DB9C, 0x14, 11, 10>();
  InstallVirtualCallThunk<0x8254DBB4, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8254DBCC, 0x2C, 11, 10>();
  InstallVirtualCallThunk<0x8256B258, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8256B280, 0x1C, 11, 10>();
  InstallVirtualCallThunk<0x8256B3D4, 0xC, 7, 6>();
  InstallVirtualCallThunk<0x8256B414, 0x10, 11, 10>();
  InstallVirtualCallThunk<0x8256B44C, 0x14, 7, 6>();
  InstallVirtualCallThunk<0x8256B48C, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8256E00C, 0x138, 11, 10>();
  InstallVirtualCallThunk<0x8256E034, 0x134, 11, 10>();
  InstallVirtualCallThunk<0x8256E26C, 0x80, 11, 10>();
  InstallVirtualCallThunk<0x8256E284, 0x84, 11, 10>();
  InstallVirtualCallThunk<0x8256E29C, 0x88, 11, 10>();
  InstallVirtualCallThunk<0x8256E2B4, 0xB8, 11, 10>();
  InstallVirtualCallThunk<0x8256E2CC, 0xBC, 11, 10>();
  InstallVirtualCallThunk<0x8256E2E4, 0xC8, 11, 10>();
  InstallVirtualCallThunk<0x8256E2FC, 0x110, 11, 10>();
  InstallVirtualCallThunk<0x8256E314, 0x114, 11, 10>();
  InstallVirtualCallThunk<0x8256E32C, 0x5C, 11, 10>();
  InstallVirtualCallThunk<0x8256E344, 0x60, 11, 10>();
  InstallVirtualCallThunk<0x8256E35C, 0x64, 11, 10>();
  InstallVirtualCallThunk<0x8256E3BC, 0x18, 11, 10>();
  InstallVirtualCallThunk<0x8256E3D4, 0x1C, 11, 10>();
  InstallVirtualCallThunk<0x8256E3EC, 0xD0, 11, 10>();
  InstallVirtualCallThunk<0x8256E404, 0x130, 11, 10>();
  InstallVirtualCallThunk<0x8256E41C, 0x124, 11, 10>();
  InstallVirtualCallThunk<0x8256E434, 0x118, 11, 10>();
  InstallVirtualCallThunk<0x8256E44C, 0x11C, 11, 10>();
  InstallVirtualCallThunk<0x8256E6BC, 0x128, 11, 10>();
  InstallVirtualCallThunk<0x8256E6D4, 0x12C, 11, 10>();
  InstallVirtualCallThunk<0x8256E824, 0xE8, 11, 10>();
  InstallVirtualCallThunk<0x8256E83C, 0xEC, 11, 10>();
  InstallVirtualCallThunk<0x8256E858, 0x70, 11, 10>();
  InstallVirtualCallThunk<0x8256E924, 0x8C, 11, 10>();
  InstallVirtualCallThunk<0x8256E998, 0x70, 11, 10>();
  InstallVirtualCallThunk<0x8256E9AC, 0x108, 11, 10>();
  InstallVirtualCallThunk<0x8256E9C4, 0x10C, 11, 10>();
  InstallVirtualCallThunk<0x8256E9DC, 0xDC, 11, 10>();
  InstallVirtualCallThunk<0x8256E9F4, 0x20, 11, 10>();
  InstallVirtualCallThunk<0x8256EA0C, 0x74, 11, 10>();
  InstallVirtualCallThunk<0x8256EA24, 0x7C, 11, 10>();
  InstallVirtualCallThunk<0x8256EA3C, 0xB0, 11, 10>();
  InstallVirtualCallThunk<0x8256EA54, 0xA8, 11, 10>();
  InstallVirtualCallThunk<0x8256EA6C, 0xD4, 11, 10>();
  InstallVirtualCallThunk<0x8256EA84, 0xD8, 11, 10>();
  InstallVirtualCallThunk<0x8256EA9C, 0x54, 11, 10>();
  InstallVirtualCallThunk<0x8256EAB4, 0x58, 11, 10>();
  InstallVirtualCallThunk<0x8256EACC, 0x68, 11, 10>();
  InstallVirtualCallThunk<0x8256EAE4, 0x6C, 11, 10>();
  InstallVirtualCallThunk<0x8256EAFC, 0xC0, 11, 10>();
  InstallVirtualCallThunk<0x8256EB14, 0xC4, 11, 10>();
  InstallVirtualCallThunk<0x8256EBDC, 0x90, 11, 10>();
  InstallVirtualCallThunk<0x8256EBF4, 0x94, 11, 10>();
  InstallVirtualCallThunk<0x8256EC0C, 0x98, 11, 10>();
  InstallVirtualCallThunk<0x8256EC24, 0x9C, 11, 10>();
  InstallVirtualCallThunk<0x8256EC3C, 0xA0, 11, 10>();
  InstallVirtualCallThunk<0x8256EC54, 0xA4, 11, 10>();
  InstallVirtualCallThunk<0x8256EC7C, 0x34, 9, 8>();
  InstallVirtualCallThunk<0x8256ED84, 0xF0, 11, 10>();
  InstallVirtualCallThunk<0x8256ED9C, 0x78, 11, 10>();
  InstallVirtualCallThunk<0x8256EDB4, 0xAC, 11, 10>();
  InstallVirtualCallThunk<0x8256EDCC, 0xB4, 11, 10>();
  InstallVirtualCallThunk<0x8267F87C, 0x0, 11, 11>();
  InstallVirtualCallThunk<0x8267F8A0, 0x4, 11, 11>();
  InstallVirtualCallThunk<0x8267F8C8, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x8267F8F0, 0x14, 11, 11>();
  InstallVirtualCallThunk<0x82686030, 0x0, 11, 11>();
  InstallVirtualCallThunk<0x82686058, 0x4, 11, 11>();
  InstallVirtualCallThunk<0x82686080, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x82691BEC, 0x28, 11, 11>();
  InstallVirtualCallThunk<0x82698814, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x82698838, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x82698874, 0x10, 11, 11>();
  InstallVirtualCallThunk<0x82698898, 0x10, 11, 11>();
  InstallVirtualCallThunk<0x82698B88, 0x8, 11, 11>();
  InstallVirtualCallThunk<0x8269E0A4, 0x1C, 11, 11>();
  InstallVirtualCallThunk<0x8269E0D0, 0xC, 11, 11>();
  InstallVirtualCallThunk<0x8269E100, 0x10, 11, 11>();

  // Tekken's retail storage bootstrap opens the optical-image device before
  // it starts requesting DATA*.IDX/BIN content.  RexGlue's extracted-tree
  // VFS mounts the same bytes at Partition1, but does not expose the retail
  // Image device name.  Alias only that exact device to the mounted game
  // volume; this keeps all normal file/content semantics on the stock VFS.
  if (StorageBootstrap() && runtime() && runtime()->file_system()) {
    // Keep the extracted-tree device for relative DATA/IDX opens, but make
    // its root handle expose the one raw volume sector the retail bootstrap
    // probes at offset 0x800.  This is an opt-in device feature; it does not
    // alter normal HostPathEntry reads.
    if (auto* partition1 = runtime()->file_system()->ResolvePath(
            "\\Device\\Harddisk0\\Partition1")) {
      if (auto* host_device = dynamic_cast<rex::filesystem::HostPathDevice*>(
              partition1->device())) {
        host_device->EnableRootSectorProbe();
        REXLOG_INFO("Tekken6 storage bootstrap: enabled Josh sector probe on game volume");
      } else {
        REXLOG_WARN("Tekken6 storage bootstrap: Partition1 is not a HostPathDevice");
      }
    } else {
      REXLOG_WARN("Tekken6 storage bootstrap: Partition1 device is unavailable");
    }
    runtime()->file_system()->RegisterSymbolicLink(
        "\\Device\\Image", "\\Device\\Harddisk0\\Partition1");
    // The retail bootstrap opens Partition0 for the image-header probe and
    // then uses that handle as the root of relative content opens.  The
    // generic SDK registers it as an empty NullDevice, which makes those
    // reads succeed with zero bytes and prevents DINF/IDX discovery.  The
    // extracted ISO tree is the truthful read-only backing for this title.
    runtime()->file_system()->RegisterSymbolicLink(
        "\\Device\\Harddisk0\\Partition0", "\\Device\\Harddisk0\\Partition1");
    REXLOG_INFO("Tekken6 storage bootstrap: aliased Image and Partition0 to game volume");


    // Diagnostic only: observe the title-side readiness/callback path
    // without changing its behavior. Keep the wrappers opt-in so normal
    // runs exercise the unmodified generated guest code.
    const bool trace_title_state =
        T6Env("TEKKEN6_TRACE_TITLE_STATE") != nullptr;
    const bool trace_startup_gate =
        T6Env("TEKKEN6_TRACE_STARTUP_GATE") != nullptr;
    if (trace_title_state || trace_startup_gate) {
      readiness_runtime_ = runtime();
      if (trace_startup_gate) {
        REXLOG_INFO(
            "Tekken6 startup-gate probe: prelaunch gate={:08X} "
            "command_line_slot={:08X}",
            ReadStartupGate(), ReadGuestWord(0x820007A8));
      }
      if (trace_title_state) {
        readiness_original_ =
            readiness_runtime_->function_dispatcher()->GetFunction(0x82219E58);
        if (readiness_original_) {
          readiness_runtime_->function_dispatcher()->SetFunction(0x82219E58,
                                                                 &ReadinessProbe);
          REXLOG_INFO("Tekken6 readiness probe installed at 82219E58");
        } else {
          REXLOG_WARN("Tekken6 readiness probe: function 82219E58 is not registered");
        }

        InstallGuestProbe<0x8221B410>();
        InstallGuestProbe<0x82219E38>();
        InstallGuestProbe<0x8246C138>();
        InstallGuestProbe<0x824C7F10>();
        InstallGuestProbe<0x824C7FB8>();
        InstallGuestProbe<0x824C80B0>();
        InstallGuestProbe<0x824CC2B0>();
        InstallGuestProbe<0x824CC358>();
        InstallGuestProbe<0x824CC638>();
        // The Xenos host dispatches the registered vblank callback through
        // this title function.  Observing it distinguishes a recorded
        // interrupt from a callback that actually reaches guest code.
        InstallGuestProbe<0x824D3AE0>();
      }

      // These are the calls which lead directly to xstart's startup gate.
      // The probes only observe register/memory state and call the original
      // generated function; they never alter the gate or launch arguments.
      if (trace_startup_gate) {
        InstallGuestProbe<0x824BEA50>();
        InstallGuestProbe<0x824CA598>();
        InstallGuestProbe<0x824C9B58>();
        InstallGuestProbe<0x826A6110>();
        InstallGuestProbe<0x824CA448>();
        InstallGuestProbe<0x824CA368>();

        // These title constructors are the only decoded routines which
        // write a +288 state word.  Probe them indirectly (the init arrays
        // call them through guest function pointers) so we can distinguish
        // "constructor never reached" from "constructor reached with a
        // failed resource/input contract".  The wrappers are diagnostic
        // only and remain behind TEKKEN6_TRACE_STARTUP_GATE.
        InstallGuestProbe<0x8221A178>();
        InstallGuestProbe<0x8221A340>();
        InstallGuestProbe<0x8221A6C0>();
        InstallGuestProbe<0x8221A720>();
        InstallGuestProbe<0x8221A7B8>();
        InstallGuestProbe<0x8221A878>();
        InstallGuestProbe<0x8221AAC8>();
        InstallGuestProbe<0x8221ABC0>();
        // The readiness object's vtable dispatches these callback targets
        // indirectly through 0x8246C138. Probe the actual targets too;
        // probing only the dispatcher cannot distinguish a callback that
        // returns an unready state from one that is never entered.
        InstallGuestProbe<0x8221AEC8>();
        InstallGuestProbe<0x8221AF40>();
        InstallGuestProbe<0x8221AFB0>();
        // These two routines contain the remaining indirect callback sites
        // on the readiness object's producer path.  Their generic probes
        // also snapshot +116/+284/+288/+292 so we can see a real transition
        // without forcing one.
        InstallGuestProbe<0x8221B480>();
        InstallGuestProbe<0x82219B70>();
        // Registration/constructor bridge: these are the points that should
        // create a resource node and link it into the +116 manager list.
        // Keep them observational so a missing call is distinguishable from
        // a callback that runs but leaves the list empty.
        InstallGuestProbe<0x82218FA0>();
        InstallGuestProbe<0x82218C08>();
        InstallGuestProbe<0x8221B020>();
        InstallGuestProbe<0x82219708>();
        InstallGuestProbe<0x82219630>();
        // The readiness poll enters this list walker before the producer
        // callbacks.  Probe it explicitly so a stuck poll can be separated
        // from a missing producer transition.
        InstallGuestProbe<0x82218410>();
        InstallGuestProbe<0x8223D128>();
        InstallGuestProbe<0x821BABA0>();
        // 0x8221AEC8 is the first callback that stops returning in the
        // bounded trace. Observe its two direct timing calls so the stall
        // can be attributed to a specific guest transition.
        InstallGuestProbe<0x825158B8>();
        InstallGuestProbe<0x82515938>();
        InstallGuestProbe<0x82267168>();
        InstallGuestProbe<0x822671A8>();
        InstallGuestProbe<0x82267390>();
        InstallGuestProbe<0x82267958>();
        // Queue/task entry points are indirect in this title. Observe the
        // worker and the callback targets to distinguish an empty queue from
        // a worker that runs but never schedules the content task.
        InstallGuestProbe<0x82526188>();
        InstallGuestProbe<0x82525278>();
        InstallGuestProbe<0x82524FF8>();
        InstallGuestProbe<0x82521230>();
        InstallGuestProbe<0x82519EC0>();
        InstallGuestProbe<0x8251C130>();
      }
    }

    if (const char* trace_movie = T6Env("TEKKEN6_TRACE_MOVIE");
        trace_movie && std::strcmp(trace_movie, "0") != 0) {
      InstallMovieProbe<0x82251E60>();
      InstallMovieProbe<0x822537A8>();
      InstallMovieProbe<0x823FED78>();
      InstallMovieProbe<0x823FD2C8>();
      InstallMovieProbe<0x826A0B10>();
      // The string xref identifies 0x824EAE08 as xbmovie, but it is not
      // emitted as a callable recomp function in this build. The surrounding
      // registered movie control points above remain observable.
    }
  }
}

// No mouse cursor over the game (user 2026-10-19: "lidem zustava mys nad hrou" - the SDK keeps it visible). The game
// has no use for it: hidden while it is over the game's window (elsewhere it shows as usual). T6_SHOW_CURSOR=1 = the
// SDK's.
static bool T6HideCursor() {
  static const bool hide = !T6Env("T6_SHOW_CURSOR");
  return hide;
}

// (the launcher shows the window and brings it to the front at the loading: a cursor standing still over it got no
// WM_SETCURSOR, so it stayed until the first mouse move - hidden at once here)
void Tekken6App::OnWindowFocusChanged(bool focused) {
  rex::ReXApp::OnWindowFocusChanged(focused);
  if (focused && T6HideCursor()) SetCursor(nullptr);
}

void Tekken6App::OnPostLaunchModule(rex::system::XThread* thread) {
  rex::ReXApp::OnPostLaunchModule(thread);
  if (T6HideCursor() && window()) window()->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
  if (frame_data_drawer_) {  // (OnCreateDialogs: the frame data on top of the guest output, painted by its thread)
    auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
    auto* presenter = graphics ? graphics->presenter() : nullptr;
    if (presenter) presenter->SetGuestOutputOverlay(frame_data_drawer_.get());
    REXLOG_INFO("T6 frame data: drawn as the guest output overlay: {}", presenter != nullptr);
  }
  const bool trace_output = T6Env("TEKKEN6_TRACE_OUTPUT") != nullptr;
  // The frame probe is a diagnostic: only when asked for (review 2026-10-18: it ran in every player's game - 24 GPU
  // read-backs and a 2.7 MB picture file per start). TEKKEN6_FRAME_PROBE_ATTEMPTS=<n> (n > 0), TEKKEN6_TRACE_OUTPUT
  // or TEKKEN6_CAPTURE_CONTINUOUS switch it on.
  {
    const char* attempts = T6Env("TEKKEN6_FRAME_PROBE_ATTEMPTS");
    const bool asked = (attempts && std::strcmp(attempts, "0") != 0) || T6Env("TEKKEN6_CAPTURE_CONTINUOUS") != nullptr;
    if (!asked && !trace_output) return;
  }
  REXLOG_INFO("Tekken6 output trace: post-launch hook active (transition-only, no image files)");
  capture_stop_.store(false);
  capture_thread_ = std::thread([this, trace_output] {
    // CaptureGuestOutput is explicitly safe from non-UI threads. Output
    // tracing samples guest pixels without saving images, so black/non-black
    // transitions can be aligned with the GPU's per-swap draw summaries.
    int probe_attempts = 24;
    if (const char* attempts = T6Env("TEKKEN6_FRAME_PROBE_ATTEMPTS")) {
      char* end = nullptr;
      const long parsed = std::strtol(attempts, &end, 10);
      if (end != attempts && *end == '\0' && parsed > 0 && parsed <= 600) {
        probe_attempts = static_cast<int>(parsed);
      }
    }
    const bool save_continuous = T6Env("TEKKEN6_CAPTURE_CONTINUOUS") != nullptr;
    const bool continuous = save_continuous || T6Env("TEKKEN6_TRACE_OUTPUT") != nullptr;
    int interval_ms = save_continuous ? 1000 : (continuous ? 250 : 500);
    if (const char* interval = T6Env("TEKKEN6_FRAME_PROBE_INTERVAL_MS")) {
      char* end = nullptr;
      const long parsed = std::strtol(interval, &end, 10);
      if (end != interval && *end == '\0' && parsed >= 250 && parsed <= 60000) {
        interval_ms = static_cast<int>(parsed);
      }
    }
    uint64_t best_non_black = 0;
    uint8_t best_max_channel = 0;
    int best_attempt = -1;
    auto capture_started = std::chrono::steady_clock::now();
    std::filesystem::path run_dir;
    std::ofstream manifest;
    uint64_t last_hash = 0;
    uint64_t sample_number = 0;
    std::vector<uint8_t> previous_rgb;
    uint32_t previous_width = 0;
    uint32_t previous_height = 0;
    bool have_output_state = false;
    bool previous_output_black = true;
    if (save_continuous) {
      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
      run_dir = cache_root() / "captures" /
                ("run_" + std::to_string(now_ms));
      std::error_code ec;
      std::filesystem::create_directories(run_dir / "frames", ec);
      if (ec) {
        REXLOG_ERROR("Frame probe: cannot create run capture directory {}: {}",
                     run_dir.string(), ec.message());
      } else {
        manifest.open(run_dir / "samples.csv", std::ios::out | std::ios::trunc);
        if (manifest) {
          manifest << "sample,elapsed_ms,wall_unix_ms,width,height,fnv1a64,"
                      "changed_pixels,changed_percent,non_black,max_channel,frame_file\n";
          manifest.flush();
          REXLOG_INFO("Frame probe: continuous capture active at {} ms; output={}",
                      interval_ms, run_dir.string());
        } else {
          REXLOG_ERROR("Frame probe: cannot open sample manifest in {}", run_dir.string());
        }
      }
    }

    for (int attempt = 0;
         !capture_stop_.load() && (continuous || attempt < probe_attempts); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
      if (capture_stop_.load()) break;
      auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
      auto* presenter = graphics ? graphics->presenter() : nullptr;
      if (!presenter) {
        if (!trace_output || attempt % 20 == 0) {
          REXLOG_WARN("Frame probe: presenter is not available yet");
        }
        continue;
      }

      rex::ui::RawImage image;
      if (!presenter->CaptureGuestOutput(image)) {
        if (!trace_output || attempt % 20 == 0) {
          REXLOG_INFO("Frame probe: no guest output image yet (attempt {})", attempt + 1);
        }
        continue;
      }
      if (image.width == 0 || image.height == 0 || image.stride < image.width * 4 ||
          image.data.size() < image.stride * image.height) {
        REXLOG_ERROR("Frame probe: invalid image {}x{} stride {} bytes {}", image.width,
                     image.height, image.stride, image.data.size());
        continue;
      }

      uint64_t non_black = 0;
      uint8_t max_channel = 0;
      uint64_t hash = 14695981039346656037ull;
      uint64_t changed_pixels = 0;
      const bool compare_previous = continuous && previous_width == image.width &&
                                    previous_height == image.height &&
                                    previous_rgb.size() == uint64_t(image.width) *
                                                                image.height * 3;
      std::vector<uint8_t> current_rgb;
      if (continuous) current_rgb.resize(size_t(image.width) * image.height * 3);
      size_t rgb_offset = 0;
      for (uint32_t y = 0; y < image.height; ++y) {
        const auto* row = image.data.data() + size_t(y) * image.stride;
        for (uint32_t x = 0; x < image.width; ++x) {
          const uint8_t r = row[x * 4 + 0];
          const uint8_t g = row[x * 4 + 1];
          const uint8_t b = row[x * 4 + 2];
          if (r || g || b) ++non_black;
          max_channel = std::max(max_channel, std::max(r, std::max(g, b)));
          if (continuous) {
            for (const uint8_t channel : {r, g, b}) {
              current_rgb[rgb_offset] = channel;
              hash ^= channel;
              hash *= 1099511628211ull;
              ++rgb_offset;
            }
            if (compare_previous &&
                (r != previous_rgb[rgb_offset - 3] ||
                 g != previous_rgb[rgb_offset - 2] ||
                 b != previous_rgb[rgb_offset - 1])) {
              ++changed_pixels;
            }
          }
        }
      }

      ++sample_number;
      const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - capture_started)
                                  .count();
      const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
      const uint64_t pixel_count = uint64_t(image.width) * image.height;
      if (!compare_previous) changed_pixels = pixel_count;
      std::string frame_file;
      if (continuous) {
        if (!compare_previous || hash != last_hash) {
          if (save_continuous) {
            std::ostringstream filename;
            filename << "frame_" << std::setw(6) << std::setfill('0') << sample_number
                     << "_t+" << elapsed_ms << "ms.ppm";
            frame_file = filename.str();
            const auto out_path = run_dir / "frames" / filename.str();
            if (!WritePpm(out_path, image)) {
              frame_file = "WRITE_FAILED";
              REXLOG_ERROR("Frame probe: cannot write {}", out_path.string());
            }
          }
        }
        const double changed_percent = compare_previous
                                           ? 100.0 * changed_pixels / pixel_count
                                           : 100.0;
        if (manifest) {
          manifest << sample_number << ',' << elapsed_ms << ',' << wall_ms << ','
                   << image.width << ',' << image.height << ",0x" << std::hex << hash
                   << std::dec << ',' << changed_pixels << ',' << std::fixed
                   << std::setprecision(4) << changed_percent << ',' << non_black << ','
                   << unsigned(max_channel) << ',' << frame_file << '\n';
          manifest.flush();
        }
        if (save_continuous) {
          REXLOG_INFO(
              "Frame probe: sample {} t+{}ms {}x{} hash=0x{:016X} changed={:.4f}% "
              "non_black={} max_channel={}",
              sample_number, elapsed_ms, image.width, image.height, hash,
              changed_percent, non_black, max_channel);
        } else if (trace_output) {
          const bool output_black = non_black == 0;
          if (!have_output_state || output_black != previous_output_black ||
              sample_number % 20 == 0) {
            REXLOG_INFO(
                "T6 output sample={} t+{}ms state={} size={}x{} non_black={}/{} "
                "changed={:.3f}% max={} hash=0x{:016X}",
                sample_number, elapsed_ms, output_black ? "BLACK" : "IMAGE", image.width,
                image.height, non_black, pixel_count, changed_percent, max_channel, hash);
          }
          previous_output_black = output_black;
          have_output_state = true;
        }
        previous_rgb.swap(current_rgb);
        previous_width = image.width;
        previous_height = image.height;
        last_hash = hash;
      } else {
        REXLOG_INFO("Frame probe: sample {} captured {}x{} (non_black={} / {}, max_channel={})",
                    attempt + 1, image.width, image.height, non_black, pixel_count, max_channel);
      }
      if (T6Env("TEKKEN6_CAPTURE_ALL") != nullptr) {
        const auto out_dir = cache_root() / "captures";
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        WritePpm(out_dir / ("guest_output_sample_" + std::to_string(attempt + 1) + ".ppm"), image);
      }
      // Once the guest has filled the output, non_black stops being useful
      // as a quality metric: a flat clear can make every pixel nonzero.
      // Prefer the sample with the highest channel value, using coverage as
      // a tie breaker so the saved diagnostic is the latest meaningful
      // frame rather than the first flat one.
      if (trace_output) {
        continue;
      }
      if (max_channel < best_max_channel ||
          (max_channel == best_max_channel && non_black <= best_non_black)) {
        continue;
      }
      best_non_black = non_black;
      best_max_channel = max_channel;
      best_attempt = attempt + 1;
      const auto out_dir = cache_root() / "captures";
      std::error_code ec;
      std::filesystem::create_directories(out_dir, ec);
      const auto out_path = out_dir / "guest_output_best.ppm";
      if (!WritePpm(out_path, image)) {
        REXLOG_ERROR("Frame probe: cannot open {}", out_path.string());
        continue;
      }
      REXLOG_INFO("Frame probe: new best sample {} written to {} (non_black={} / {}, max_channel={})",
                  best_attempt, out_path.string(), best_non_black,
                  uint64_t(image.width) * image.height, best_max_channel);
    }
    if (manifest) {
      manifest.flush();
      manifest.close();
    }
    if (save_continuous) {
      REXLOG_INFO("Frame probe: continuous capture stopped after {} samples (best_sample={} "
                  "non_black={} max_channel={}); output={}",
                  sample_number, best_attempt, best_non_black, best_max_channel,
                  run_dir.string());
    } else if (!trace_output) {
      REXLOG_INFO("Frame probe: bounded sampling complete (best_sample={} non_black={} max_channel={})",
                  best_attempt, best_non_black, best_max_channel);
    } else {
      REXLOG_INFO("T6 output trace: stopped after {} samples", sample_number);
    }
  });
}

bool Tekken6App::WritePpm(const std::filesystem::path& path, const rex::ui::RawImage& image) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
  for (uint32_t y = 0; y < image.height; ++y) {
    const auto* row = image.data.data() + size_t(y) * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) out.write(reinterpret_cast<const char*>(row + x * 4), 3);
  }
  return bool(out);
}

void Tekken6App::CallOriginal(uint32_t address, PPCContext& ctx, uint8_t* base) {
  auto it = GuestProbeOriginals().find(address);
  if (it != GuestProbeOriginals().end() && it->second) it->second(ctx, base);
}

std::string Tekken6App::ProducerWords(uint32_t object) {
  return fmt::format("+116={:08X} +284={:08X} +288={:08X} +292={:08X} +296={:08X}", ReadGuestWord(object + 116),
                     ReadGuestWord(object + 284), ReadGuestWord(object + 288), ReadGuestWord(object + 292),
                     ReadGuestWord(object + 296));
}

std::unordered_map<uint32_t, ::PPCFunc*>& Tekken6App::GuestProbeOriginals() {
  static std::unordered_map<uint32_t, ::PPCFunc*> originals;
  return originals;
}

uint32_t Tekken6App::ReadGuestWord(uint32_t address) {
  if (!readiness_runtime_ || !readiness_runtime_->memory()) {
    return 0xFFFFFFFFu;
  }
  return rex::memory::load_and_swap<uint32_t>(
      readiness_runtime_->memory()->TranslateVirtual(address));
}

void Tekken6App::ReadinessProbe(PPCContext& ctx, uint8_t* base) {
  const uint32_t object = ctx.r3.u32;
  // the readiness words +284.. and the callback slots +96.. (4 each)
  const auto words = [object](uint32_t from, uint32_t out[4]) {
    for (uint32_t i = 0; i < 4; ++i) out[i] = ReadGuestWord(object + from + i * 4u);
  };
  uint32_t before[4] = {};
  uint32_t callback_before[4] = {};
  const bool valid = readiness_runtime_ && object >= 0x80000000u && object < 0xC0000000u;
  if (valid) {
    words(284, before);
    words(96, callback_before);
  }
  const uint32_t call = readiness_calls_.fetch_add(1);
  if (call < 64 || (valid && before[1] != 0)) {
    const uint32_t vtbl = ReadGuestWord(object + 0);
    REXLOG_INFO("Readiness probe #{} object={:08X} vtbl={:08X} vtbl20={:08X} +16={:08X} +48={:08X} +52={:08X} +60={:08X} +64={:08X} +72={:08X} +116={:08X} +152={:08X} +156={:08X} +160={:08X} +164={:08X} before=({}, {}, {}, {}) "
                "callback=({:08X},{:08X},{:08X},{:08X}) valid={}", call,
                object, vtbl, ReadGuestWord(vtbl + 20), ReadGuestWord(object + 16),
                ReadGuestWord(object + 48), ReadGuestWord(object + 52),
                ReadGuestWord(object + 60), ReadGuestWord(object + 64),
                ReadGuestWord(object + 72), ReadGuestWord(object + 116),
                ReadGuestWord(object + 152), ReadGuestWord(object + 156),
                ReadGuestWord(object + 160), ReadGuestWord(object + 164),
                before[0], before[1], before[2], before[3],
                callback_before[0], callback_before[1], callback_before[2],
                callback_before[3], valid);
  }
  readiness_original_(ctx, base);
  if (valid && (call < 64 || before[1] != 0)) {
    uint32_t after[4] = {};
    uint32_t callback_after[4] = {};
    words(284, after);
    words(96, callback_after);
    REXLOG_INFO("Readiness probe #{} return={} after=({}, {}, {}, {}) "
                "callback=({:08X},{:08X},{:08X},{:08X})", call, ctx.r3.u32,
                after[0], after[1], after[2], after[3], callback_after[0],
                callback_after[1], callback_after[2], callback_after[3]);
  }
}
