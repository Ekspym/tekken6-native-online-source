#include "t6_advanced_practice.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include "practice/t6_frame_data_panels.h"
#include "practice/t6_position_snapshot.h"
#include "practice/tekken6_record_hud_font.inc"
extern std::atomic<int> selectedRecordSlot;
extern std::atomic<bool> recordSlotActive;
extern std::atomic<bool> recordPlaybackModeActive;
extern std::atomic<bool> recordSlotRecording;
extern std::atomic<unsigned> recordTimeRemainingMillis; // RECORD HUD FINISH V1
extern std::atomic<bool> opponentToggleTest;
extern std::atomic<unsigned> firstPlaybackState;

uint64_t TekkenFrameDataPanels();
namespace { ImFont* font=nullptr;
void DrawRecord(ImGuiIO& io) {
    const int slot=selectedRecordSlot.load(std::memory_order_acquire);
    const bool hasSlot=slot>=0 && slot<8 && recordSlotActive.load(std::memory_order_acquire);
    if(!hasSlot && !recordPlaybackModeActive.load(std::memory_order_acquire)) return;
    const bool recording=recordSlotRecording.load(std::memory_order_acquire);
    const bool controlling=opponentToggleTest.load(std::memory_order_relaxed);
    const bool playing=firstPlaybackState.load(std::memory_order_acquire)==1;
    if(font) ImGui::PushFont(font); // RECORD HUD FONT V1
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.17f),ImGuiCond_Always,ImVec2(0.5f,0.0f));
    // RECORD MENU CONTROLS V1: translucent navy blue, white HUD text.
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.02f,0.06f,0.18f,0.65f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.0f,1.0f,1.0f,1.0f));
    // RECORD RESET TOGGLE V1: replace the inherited green outline.
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.01f,0.025f,0.08f,0.95f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow,ImVec4(0.0f,0.01f,0.04f,0.40f));
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGuiWindowFlags flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
      ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin("##tekken_record_hud",nullptr,flags)) {
      // RECORD HUD FINISH V1: countdown begins after the start chord is released.
      if(hasSlot) ImGui::Text("SLOT %d",slot+1);
      else ImGui::TextUnformatted("RECORD/PLAYBACK");
      if(recording) ImGui::TextUnformatted("Start: Finish recording");
      else if(playing) ImGui::TextUnformatted("Select + B: Stop playback"); // LOOP PLAYBACK TEST V1
      else if(controlling) ImGui::TextUnformatted("Select + X: Start recording");
      else ImGui::TextUnformatted("Select + B: Play recording");
      if(recording) {
        const unsigned ms=recordTimeRemainingMillis.load(std::memory_order_acquire);
        ImGui::Text("Time left: %.1f s",static_cast<double>(ms)/1000.0);
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(4); // RECORD RESET TOGGLE V1
    if(font) ImGui::PopFont(); // RECORD HUD FONT V1
}
void DrawPositionSnapshot(ImGuiIO& io) {
    if(!T6PositionSnapshotAvailable())return;
    using t6_position_snapshot::Status;
    const auto status=static_cast<Status>(T6PositionSnapshotStatus());
    const char* note=status==Status::Saved?"State saved":status==Status::Loaded?"State loaded":
      status==Status::NoSave?"No saved state":status==Status::Changed?"State cleared":
      status==Status::Invalid?"Cannot save state":status==Status::Failed?"Load unavailable":"No saved state";
    if(font)ImGui::PushFont(font);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f,io.DisplaySize.y*0.985f),ImGuiCond_Always,ImVec2(0.5f,1.0f));
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.02f,0.06f,0.18f,0.65f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,1,1,1));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.01f,0.025f,0.08f,0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(6,2));
    const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
      ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin("##tekken_position_snapshot",nullptr,flags)) {
      char prompt[192];
      std::snprintf(prompt,sizeof(prompt),"Select + LT: Save state    Select + RT: Load state    [%s]",note);
      ImGui::SetWindowFontScale(1.0f);
      const float full=ImGui::CalcTextSize(prompt).x,room=io.DisplaySize.x-28.0f;
      if(room>0 && full>room)ImGui::SetWindowFontScale(room/full);
      ImGui::TextUnformatted(prompt);
    }
    ImGui::End();ImGui::PopStyleVar();ImGui::PopStyleColor(3);
    if(font)ImGui::PopFont();
}
void DrawFramePanel(ImGuiIO& io,unsigned player,uint64_t data) {
    if(!T6FrameDataVisible(player))return;
    if(font)ImGui::PushFont(font);
    // Below COUNTER/WALL HIT, separated from the bottom native prompts.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*(player?0.93f:0.07f),io.DisplaySize.y*0.49f),ImGuiCond_Always,ImVec2(player?1.0f:0.0f,0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0.02f,0.06f,0.18f,0.65f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,1,1,1));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.01f,0.025f,0.08f,0.95f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow,ImVec4(0,0.01f,0.04f,0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12,8));
    const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
      ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin(player?"##tekken_frame_data_p2":"##tekken_frame_data_p1",nullptr,flags)) {
      ImGui::Text("PLAYER %u",player+1);
      const unsigned startup=t6_frame_panels::Startup(data,player);
      if(startup)ImGui::Text("STARTUP       %uF",startup);
      else ImGui::TextUnformatted("STARTUP       --");
      if(t6_frame_panels::Valid(data)) {
        const int advantage=t6_frame_panels::Advantage(data,player);
        const ImVec4 color=advantage>0?ImVec4(0.2f,1.0f,0.35f,1.0f):
                          advantage<0?ImVec4(1.0f,0.25f,0.25f,1.0f):ImVec4(1,1,1,1);
        if(advantage)ImGui::TextColored(color,"FRAME ADVANTAGE  %+dF",advantage);
        else ImGui::TextUnformatted("FRAME ADVANTAGE  0F");
      } else ImGui::TextUnformatted("FRAME ADVANTAGE  --");
    }
    ImGui::End();ImGui::PopStyleVar(2);ImGui::PopStyleColor(4);
    if(font)ImGui::PopFont();
}
}
void T6AdvancedConfigureFonts(ImFontAtlas* atlas) {
  if (T6AdvancedRequested()) font=tekken_record_hud_font::Load(atlas);
}
void T6AdvancedDraw(ImGuiIO& io) {
  if (!T6AdvancedAvailable()) return;
  DrawPositionSnapshot(io);
  if (const char* flag=T6PracticeEnv("TEKKEN6_TEST_RECORD_HUD"); flag && *flag=='1') DrawRecord(io);
  if(T6FrameDataVisible(0) || T6FrameDataVisible(1)) {
    const uint64_t snapshot=TekkenFrameDataPanels();
    DrawFramePanel(io,0,snapshot);
    DrawFramePanel(io,1,snapshot);
  }
}
