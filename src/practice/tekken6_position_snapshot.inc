// Full offline practice checkpoint at the main-loop frame boundary.
#pragma once
#include "t6_position_snapshot.h"
#include <memory>
#include "t6_game_checkpoint.h"
#include "t6_after_contact.h"
namespace position_snapshot_detail {
using namespace t6_position_snapshot;
std::atomic<bool> available{false};
std::atomic<uint32_t> controller{0};
std::atomic<Action> pending{Action::None};
std::atomic<Status> status{Status::Empty};
std::atomic<uint64_t> sceneEpoch{0};
std::mutex inputMutex;
Chords chords;
Scope scope;
struct HostState {
  t6_after_contact::Runtime afterContact;
  uint32_t samples[1800]{},weightedSamples[1800]{};
  unsigned count=0,position=0,playState=0,current=0,reads=0,weightedCount=0,weightMode=0,remaining=0;
  int selected=-1;
  bool weighted=false,active=false,mode=false,recording=false,controlling=false;
  RecordingCycleState cycle{};
  std::chrono::steady_clock::duration elapsed{};
  std::mt19937 random;
  tekken_contacts_measure::Meter meter;
  uint32_t meterOwner=0;
  uint64_t frameResult=0,framePanels=0;
  std::map<uint32_t,uint32_t> cameraChoices;
  bool cameraRight=false,cameraInside=false;
  unsigned cameraTrace=0;
  void Capture() {
    afterContact=after_contact_detail::state;
    std::memcpy(samples,firstPlaybackSamples,sizeof(samples));
    std::memcpy(weightedSamples,weightedPlaybackSamples,sizeof(weightedSamples));
    count=firstPlaybackCount;position=firstPlaybackPosition;weightedCount=weightedPlaybackCount;weighted=weightedPlaybackActive;
    playState=firstPlaybackState.load();current=firstPlaybackCurrent.load();reads=firstPlaybackReads.load();
    selected=selectedRecordSlot.load();active=recordSlotActive.load();mode=recordPlaybackModeActive.load();
    recording=recordSlotRecording.load();controlling=opponentToggleTest.load();
    weightMode=recordWeightMenuMode.load();remaining=recordTimeRemainingMillis.load();
    cycle=recordingCycleState;random=recordingPlaybackRandom;
    cameraChoices=practiceCameraChoices;cameraRight=rightSideCameraRequested;
    cameraInside=insidePracticeCameraReset;cameraTrace=cameraUpdatesToTrace;
    if(recording)elapsed=std::chrono::steady_clock::now()-cycle.started;
    std::lock_guard<std::mutex> lock(tekken_contacts_live::mutex);
    meter=tekken_contacts_live::meter;meterOwner=tekken_contacts_live::owner;
    frameResult=tekken_contacts_live::result.load();framePanels=tekken_contacts_live::panels.load();
  }
  void Put() const {
    after_contact_detail::state=afterContact;after_contact_detail::inputTarget=false;
    std::memcpy(firstPlaybackSamples,samples,sizeof(samples));
    std::memcpy(weightedPlaybackSamples,weightedSamples,sizeof(weightedSamples));
    firstPlaybackCount=count;firstPlaybackPosition=position;weightedPlaybackCount=weightedCount;weightedPlaybackActive=weighted;
    firstPlaybackState.store(playState);firstPlaybackCurrent.store(current);firstPlaybackReads.store(reads);
    selectedRecordSlot.store(selected);recordSlotActive.store(active);recordPlaybackModeActive.store(mode);
    recordSlotRecording.store(recording);opponentToggleTest.store(controlling);
    recordWeightMenuMode.store(weightMode);recordTimeRemainingMillis.store(remaining);
    recordingCycleState=cycle;recordingPlaybackRandom=random;
    practiceCameraChoices=cameraChoices;rightSideCameraRequested=cameraRight;
    insidePracticeCameraReset=cameraInside;cameraUpdatesToTrace=cameraTrace;
    if(recording)recordingCycleState.started=std::chrono::steady_clock::now()-elapsed;
    // Deferred UI transitions belong to the current real input stream, not the saved RAM.
    recordFinishStartRequested.store(false);recordFinishStartHeld.store(false);
    recordPauseRequestedOwner.store(0);recordMenuCloseRequestedOwner.store(0);
    opponentControlSuppressP1=false;firstPlaybackTarget=false;
    std::lock_guard<std::mutex> lock(tekken_contacts_live::mutex);
    tekken_contacts_live::meter=meter;tekken_contacts_live::owner=meterOwner;
    tekken_contacts_live::result.store(frameResult);tekken_contacts_live::panels.store(framePanels);
  }
};
struct Checkpoint {
  SnapTest game{false};HostState host;
  Key key;uint64_t epoch=0,rows=0;
};
std::unique_ptr<Checkpoint> checkpoint;
void log(const char* event,size_t bytes=0) {
  if(auto* f=std::fopen("practice-state-snapshot.txt","a")) {
    std::fprintf(f,"%s bytes=%zu scene=%llu rows=%llu\n",event,bytes,
      static_cast<unsigned long long>(sceneEpoch.load()),static_cast<unsigned long long>(T6RowsGeneration()));
    auto* b=quick_select_apply_detail::memory();
    if(b) {
      const uint32_t taskHeap=quick_select_apply_detail::read(b,0x828B3A10u);
      std::fprintf(f,"task_allocator=%08X write_kinds=%02X sound_separate=%u\n",taskHeap,
        unsigned(t6_pagemap[taskHeap>>12]),T6Env("T6_SOUND_SEPARATE")?1u:0u);
    }
    for(uint32_t fighter:{practiceRageP1.load(),practiceRageP2.load()}) {
      if(!b || !quick_select_apply_detail::span(fighter,0x800))continue;
      std::fprintf(f,"fighter=%08X move=%08X animation_frame=%u command=%08X health=%u\n",fighter,
        quick_select_apply_detail::read(b,fighter+0x1C0),quick_select_apply_detail::read(b,fighter+0xC4),
        quick_select_apply_detail::read(b,fighter+0x108),quick_select_apply_detail::read(b,fighter+0x60));
    }
    std::fclose(f);
  }
}
bool covers(const SnapTest& snapshot,uint32_t a,uint32_t size) {
  for(const auto& region:snapshot.snap)
    if(a>=region.addr && uint64_t(a)+size<=uint64_t(region.addr)+region.size)return true;
  return false;
}
}
void InvalidatePracticeCheckpoint(){position_snapshot_detail::sceneEpoch.fetch_add(1,std::memory_order_acq_rel);}
unsigned T6PositionSnapshotStatus(){return unsigned(position_snapshot_detail::status.load(std::memory_order_acquire));}
bool T6PositionSnapshotAvailable(){return position_snapshot_detail::available.load(std::memory_order_acquire);}
void T6PositionSnapshotPad(uint32_t user,uint8_t* pad) {
  using namespace position_snapshot_detail;
  if(!pad || user!=controller.load(std::memory_order_acquire))return;
  const bool enabled=T6AdvancedGuestAvailable() && available.load(std::memory_order_acquire);
  const uint16_t buttons=uint16_t(uint16_t(pad[4])<<8|pad[5]);
  std::lock_guard<std::mutex> lock(inputMutex);
  const auto decision=chords.Poll(enabled,buttons,pad[6],pad[7]);
  if(!decision.consume)return;
  pad[5]&=uint8_t(~0x20u);pad[6]=pad[7]=0;
  if(decision.action!=Action::None) {
    Action empty=Action::None;
    pending.compare_exchange_strong(empty,decision.action,std::memory_order_acq_rel);
  }
}
void TickPositionSnapshot(bool allow,bool gameplay) {
  using namespace position_snapshot_detail;
  using quick_select_apply_detail::memory;
  using quick_select_apply_detail::read;
  using quick_select_apply_detail::span;
  using quick_select_apply_detail::context;
  using quick_select_apply_detail::stageIndex;
  available.store(false,std::memory_order_release);
  const Action action=pending.exchange(Action::None,std::memory_order_acq_rel);
  auto* b=memory();const uint32_t owner=practiceRageOwner.load(std::memory_order_acquire);
  if(!allow || !b || !span(owner,420) || context(b)!=owner || read(b,owner)!=0x8207D3E4u) {
    checkpoint.reset();scope.Clear();status.store(Status::Empty,std::memory_order_release);return;
  }
  controller.store(read(b,owner+88)?1:0,std::memory_order_release);
  if(!gameplay)return;
  const uint32_t fighters[]={practiceRageP1.load(),practiceRageP2.load()};
  const uint32_t choices[]={read(b,owner+356),read(b,owner+360)};
  if(!span(fighters[0],0x800) || !span(fighters[1],0x800) || fighters[0]==fighters[1] || choices[0]>=102 || choices[1]>=102)return;
  const Key key{read(b,fighters[0]+88),read(b,fighters[1]+88),read(b,owner+352)};
  if(key.player!=read(b,0x82744AA8u+choices[0]*108) || key.opponent!=read(b,0x82744AA8u+choices[1]*108) || stageIndex(key.stage)==0xFFFFFFFFu)return;
  const auto epoch=sceneEpoch.load(std::memory_order_acquire),rows=T6RowsGeneration();
  const bool changed=scope.Observe(key) || (checkpoint && (checkpoint->epoch!=epoch || checkpoint->rows!=rows));
  if(changed){checkpoint.reset();status.store(Status::Changed,std::memory_order_release);log("practice-context-changed");}
  available.store(true,std::memory_order_release);
  if(action==Action::None || changed)return;
  if(!g_main_ctx) {
    const auto current=T6Rex().current_guest_context;
    g_main_ctx=current?static_cast<PPCContext*>(current()):nullptr;
  }
  if(!g_main_ctx || !T6Rex().global_lock || !T6Rex().live_guest_objects || T6Env("T6_NO_GLOBAL_LOCK")) {
    status.store(Status::Failed,std::memory_order_release);log("snapshot-runtime-unavailable");return;
  }
  if(action==Action::Save) {
    // Restoring task nodes without the allocator's live bookkeeping corrupts the
    // next task allocation. A full core-memory image alone is not sufficient.
    const uint32_t taskHeap=read(b,0x828B3A10u);
    if(taskHeap<0x40000000u || taskHeap>=0x50000000u || !(t6_pagemap[taskHeap>>12]&1u) ||
       !T6Env("T6_SOUND_SEPARATE") || !T6Env("T6_SNAP_EXCLUDE_FILE")) {
      status.store(Status::Invalid,std::memory_order_release);log("snapshot-tracking-or-launch-config-missing");return;
    }
    auto next=std::make_unique<Checkpoint>();next->key=key;next->epoch=epoch;next->rows=rows;
    next->host.Capture();t6np::SavePracticeImage(next->game);
    if(!next->game.bytes || !covers(next->game,fighters[0],0x800) || !covers(next->game,fighters[1],0x800) ||
       !covers(next->game,owner,420) || !covers(next->game,taskHeap,0x80) ||
       epoch!=sceneEpoch.load() || rows!=T6RowsGeneration()) {
      status.store(Status::Invalid,std::memory_order_release);log("incomplete-state-image",next->game.bytes);return;
    }
    checkpoint=std::move(next);status.store(Status::Saved,std::memory_order_release);log("state-saved",checkpoint->game.bytes);
  } else {
    if(!checkpoint){status.store(Status::NoSave,std::memory_order_release);log("no-saved-state");return;}
    log("load-begin",checkpoint->game.bytes);
    bool ok=false;
    {
      T6VoicesBeforeLoad();
      struct FinishAudio {~FinishAudio(){T6VoicesAfterLoad();}} finishAudio;
      log("load-audio-captured",checkpoint->game.bytes);
      ok=t6np::LoadPracticeImage(checkpoint->game);
      log(ok?"load-image-restored":"load-image-failed",checkpoint->game.bytes);
      if(ok){checkpoint->host.Put();log("load-host-restored",checkpoint->game.bytes);}
    }
    log("load-audio-finished",checkpoint->game.bytes);
    status.store(ok?Status::Loaded:Status::Failed,std::memory_order_release);
    log(ok?"state-loaded":"state-load-failed",checkpoint->game.bytes);
  }
}
