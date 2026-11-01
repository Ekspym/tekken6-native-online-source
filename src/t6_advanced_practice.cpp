// Practice features from JalenDmarion25/tekken6-recomp feature/practice-mode.
// Keep the online project's platform input and rollback hooks authoritative.
#include "t6_netplay_internal.h"
#include "t6_advanced_practice.h"
#include <cstring>
#include <memory>
#include "practice/tekken6_practice_preferences.inc"
namespace {
std::atomic<bool> advancedAvailable{false};
std::atomic<bool> manualSelection{false};
std::atomic<int> frameDataPreference{-1};
std::atomic<bool> frameDataVisible{false};
}
bool T6AdvancedRequested() {
  return T6EnvDefaultOn("T6_ADVANCED_PRACTICE") &&
      (T6Env("T6_PRACTICE") || !T6Env("T6_NET"));
}
bool T6AdvancedAvailable() { return advancedAvailable.load(std::memory_order_acquire); }
extern "C" bool T6AdvancedGuestAvailable() { return T6AdvancedRequested() && !T6NetRunning() && !T6NetResim(); }
bool T6AdvancedManualSelection() { return manualSelection.load(std::memory_order_acquire); }
const char* T6PracticeEnv(const char* name) {
  if (const char* explicitValue=T6Env(name)) return explicitValue;
  if (!std::strcmp(name,"TEKKEN6_TEST_FRAME_DATA_HUD") && T6Env("T6_PRACTICE") && !T6Env("T6_FRAME_DATA")) return nullptr;
  static const char* const features[]={"TEKKEN6_TEST_OPPONENT_CONTROL","TEKKEN6_TEST_RECORD_CYCLE","TEKKEN6_TEST_RECORD_SLOTS","TEKKEN6_TEST_RECORD_HUD","TEKKEN6_TEST_WEIGHTED_PLAYBACK","TEKKEN6_TEST_LOOP_PLAYBACK","TEKKEN6_TEST_RECORD_MODE","TEKKEN6_TEST_FRAME_DATA_HUD"};
  for (const char* feature:features) if (!std::strcmp(feature,name)) return "1";
  return nullptr;
}
// The launcher supplies the initial choice; native menu edits last for this session.
bool T6FrameDataEnabled(unsigned player) {
  if(player>1)return false;
  int value=frameDataPreference.load(std::memory_order_acquire);
  if(value<0) {
    const char* initial=T6PracticeEnv("TEKKEN6_TEST_FRAME_DATA_HUD");
    int unset=-1;
    int defaults=initial && *initial=='1'?3:0;
    for(unsigned side=0;side<2;++side){
      const int saved=T6SavedPracticeSetting(side?t6_practice_preferences::OpponentFrameData:t6_practice_preferences::PlayerFrameData);
      if(saved>=0)defaults=(defaults&~(1<<side))|(saved<<side);
    }
    frameDataPreference.compare_exchange_strong(unset,defaults);
    value=frameDataPreference.load(std::memory_order_acquire);
  }
  return (value&(1<<player))!=0;
}
void T6SetFrameDataEnabled(unsigned player,bool enabled) {
  if(player>1)return;
  T6FrameDataEnabled(player); // Initialize both defaults before editing one side.
  if(enabled)frameDataPreference.fetch_or(1<<player,std::memory_order_acq_rel);
  else frameDataPreference.fetch_and(~(1<<player),std::memory_order_acq_rel);
  T6SavePracticeSetting(player?t6_practice_preferences::OpponentFrameData:t6_practice_preferences::PlayerFrameData,enabled?1:0);
}
bool T6FrameDataVisible(unsigned player) {
  return T6FrameDataEnabled(player) && frameDataVisible.load(std::memory_order_acquire);
}
void T6AdvancedMarkSelection() {
  if (manualSelection.exchange(true,std::memory_order_acq_rel)) return;
  if (const char* status=T6Env("T6_NET_STATUS")) {
    const std::string name=std::string(status)+".selection-ready";
    if (auto* f=std::fopen(name.c_str(),"w")) { std::fputs("ready\n",f); std::fclose(f); }
  }
}
#include "practice/tekken6_guest_hooks.cpp"
#include "practice/tekken6_after_contact.inc"
#include "practice/tekken6_position_snapshot.inc"
void T6AdvancedTick() {
  const bool allow=T6AdvancedRequested() && !T6NetRunning() && !T6NetResim();
  const bool previous=advancedAvailable.exchange(allow,std::memory_order_acq_rel);
  if (!allow && previous) {
    g_practice_battle=false;
    weightedPlaybackActive=false;
    opponentControlSuppressP1=false;
    firstPlaybackTarget=false;
    recordFinishStartRequested.store(false,std::memory_order_release);
    recordFinishStartHeld.store(false,std::memory_order_release);
    recordMenuCloseRequestedOwner.store(0,std::memory_order_release);
    recordPauseRequestedOwner.store(0,std::memory_order_release);
    practiceRageOwner.store(0,std::memory_order_release);
    recordPlaybackModeActive.store(false,std::memory_order_release);
    recordSlotActive.store(false,std::memory_order_release);
    recordSlotRecording.store(false,std::memory_order_release);
    opponentToggleTest.store(false,std::memory_order_release);
    firstPlaybackState.store(0,std::memory_order_release);
    std::lock_guard<std::mutex> lock(quick_select_apply_detail::mutex);
    quick_select_apply_detail::phase=0;
  }
  t6_manual_transition::tick();
  if (allow) g_practice_battle=practiceRageOwner.load(std::memory_order_acquire)!=0;
  // Only the emulation thread reads guest state. The UI consumes this atomic snapshot.
  bool visible=false;
  if(allow) {
    using namespace quick_select_apply_detail;
    std::lock_guard<std::mutex> lock(mutex);
    auto* b=memory();
    const uint32_t owner=practiceRageOwner.load(std::memory_order_acquire);
    if(b && span(owner,420) && context(b)==owner && read(b,owner)==0x8207D3E4u)
      visible=read(b,owner+96)==0 && !active() &&
        TaskTreeHas(0x8200C5E8u,0x8200C5E8u);
  }
  frameDataVisible.store(visible,std::memory_order_release);
  TickAfterContact(allow,visible);
  TickPositionSnapshot(allow,visible);
}
void Advanced_TraceTekkenButtons(PPCRegister& r12,PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceTekkenButtons(r12,r3);
}
void Advanced_CapturePracticeHeld(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  CapturePracticeHeld(r3);
}
void Advanced_CapturePracticeResetPress(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  CapturePracticeResetPress(r3);
}
void Advanced_BeginPracticeSideReset(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  BeginPracticeSideReset(r3);
}
void Advanced_RememberPracticePlayerOne(PPCRegister& r29) {
  if (!T6AdvancedGuestAvailable()) return;
  RememberPracticePlayerOne(r29);
}
void Advanced_ApplyPracticeResetSide(PPCRegister& r29,PPCRegister& r27,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  ApplyPracticeResetSide(r29,r27,r4);
}
void Advanced_TraceTekkenStateCommand(PPCRegister& r12,PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceTekkenStateCommand(r12,r3);
}
void Advanced_RedirectPracticeFirstPlacement(PPCRegister& r3,PPCRegister& r27,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  RedirectPracticeFirstPlacement(r3,r27,r4);
}
void Advanced_InstallPracticeRageMenu(PPCRegister& r9,PPCRegister& r8) {
  if (!T6AdvancedGuestAvailable()) return;
  InstallPracticeRageMenu(r9,r8);
}
void Advanced_RedirectPracticeSecondPlacement(PPCRegister& r3,PPCRegister& r29) {
  if (!T6AdvancedGuestAvailable()) return;
  RedirectPracticeSecondPlacement(r3,r29);
}
void Advanced_BeginPracticeCameraReset(PPCRegister& r6) {
  if (!T6AdvancedGuestAvailable()) return;
  BeginPracticeCameraReset(r6);
}
void Advanced_SelectPracticeResetCamera(PPCRegister& r28,PPCRegister& r31) {
  if (!T6AdvancedGuestAvailable()) return;
  SelectPracticeResetCamera(r28,r31);
}
void Advanced_StylePracticeRageNormal(PPCRegister& r31,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  StylePracticeRageNormal(r31,r4);
}
void Advanced_EndPracticeCameraReset(PPCRegister& r24) {
  if (!T6AdvancedGuestAvailable()) return;
  EndPracticeCameraReset(r24);
}
void Advanced_TraceTekkenRage(PPCRegister& r11,PPCRegister& r10) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceTekkenRage(r11,r10);
}
void Advanced_BeginPracticeRage(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if (!T6AdvancedGuestAvailable()) return;
  InvalidatePracticeCheckpoint();
  ResetAfterContact();
  RestorePracticeSettings(); // Before the native practice initializer reads its selectors.
  BeginPracticeRage(r3,r4,r5);
}
void Advanced_EndPracticeRage(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  frameDataVisible.store(false,std::memory_order_release);
  ResetAfterContact();
  InvalidatePracticeCheckpoint();
  EndPracticeRage(r3);
}
void Advanced_ApplyPracticeRageMenuThreshold(PPCRegister& r28,PPCRegister& r29,PPCRegister& r31) {
  if (!T6AdvancedGuestAvailable()) return;
  ApplyPracticeRageMenuThreshold(r28,r29,r31);
}
void Advanced_TracePracticeMenuValue(PPCRegister& r11,PPCRegister& r10) {
  if (!T6AdvancedGuestAvailable()) return;
  TracePracticeMenuValue(r11,r10);
}
void Advanced_TraceQuickSelectMenu(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,PPCRegister& r6) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectMenu(r3,r4,r5,r6);
}
void Advanced_TraceQuickSelectCharacterScreen(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectCharacterScreen(r3);
}
void Advanced_TraceQuickSelectMenuClose(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectMenuClose(r3);
}
void Advanced_TraceQuickSelectPractice(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectPractice(r3,r4,r5);
}
void Advanced_TraceQuickSelectFighterSetting(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectFighterSetting(r3,r4,r5);
}
void Advanced_TraceQuickSelectFighterId(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceQuickSelectFighterId(r3,r4);
}
void Advanced_TraceStageBankRequest(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceStageBankRequest(r3,r4);
}
void Advanced_TraceStageBankLoad(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceStageBankLoad(r3);
}
void Advanced_QuickSelectMenuOpening(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectMenuOpening(r3);
}
void Advanced_QuickSelectSyncMenu(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectSyncMenu(r3);
}
void Advanced_QuickSelectApplyAction(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectApplyAction(r3,r4);
}
void Advanced_QuickSelectRedirectMode(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectRedirectMode(r3,r4);
}
void Advanced_QuickSelectBypassScreen(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectBypassScreen(r3,r4);
}
void Advanced_QuickSelectLoadCharacter(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectLoadCharacter(r3,r4,r5);
}
void Advanced_QuickSelectLoadStage(PPCRegister& r3,PPCRegister& r4) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectLoadStage(r3,r4);
}
void Advanced_QuickSelectObservePractice(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectObservePractice(r3,r4,r5);
}
void Advanced_QuickSelectAutoConfirmCharacter(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,PPCRegister& r7) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectAutoConfirmCharacter(r3,r4,r5,r7);
}
void Advanced_QuickSelectAutoConfirmStage(PPCRegister& r29,PPCRegister& r27,PPCRegister& r31,PPCRegister& r28) {
  if (!T6AdvancedGuestAvailable()) return;
  QuickSelectAutoConfirmStage(r29,r27,r31,r28);
}
void Advanced_TraceFighterInputLookup(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  TraceFighterInputLookup(r3);
}
void Advanced_OpponentControlSelectInput(PPCRegister& r29,PPCRegister& r30) {
  if (!T6AdvancedGuestAvailable()) return;
  OpponentControlSelectInput(r29,r30);
}
void Advanced_NativeDummyControlTest(PPCRegister& r4,PPCRegister& r30) {
  if (!T6AdvancedGuestAvailable()) return;
  NativeDummyControlTest(r4,r30);
}
void Advanced_BeginRecordMenuCloseQuery(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  BeginRecordMenuCloseQuery(r3);
}
void Advanced_FinishRecordMenuCloseQuery(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  FinishRecordMenuCloseQuery(r3);
}
void Advanced_RecordFinishPauseInput(PPCRegister& r3,PPCRegister& r25,PPCRegister& r31) {
  if (!T6AdvancedGuestAvailable()) return;
  RecordFinishPauseInput(r3,r25,r31);
}
void Advanced_HideRecordControllerPrompt(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  HideRecordControllerPrompt(r3);
}
void Advanced_ObserveRecordingStartRelease(PPCRegister& r3) {
  if (!T6AdvancedGuestAvailable()) return;
  ObserveRecordingStartRelease(r3);
}
void Advanced_GuardRecordPlaybackRootBack(PPCRegister& r26,PPCRegister& r30) {
  if (!T6AdvancedGuestAvailable()) return;
  GuardRecordPlaybackRootBack(r26,r30);
}
void Advanced_AdjustNativePracticeHud(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9,PPCRegister& r10,PPCRegister& r12) {
  if (!T6AdvancedGuestAvailable()) return;
  AdjustNativePracticeHud(r3,r4,r5,r6,r7,r8,r9,r10,r12);
}
void Advanced_MoveNativePracticePromptFrame(PPCRegister& r31,PPCRegister& r22,PPCRegister& r23) {
  if (!T6AdvancedGuestAvailable()) return;
  MoveNativePracticePromptFrame(r31,r22,r23);
}
