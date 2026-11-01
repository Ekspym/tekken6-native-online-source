#include <rex/ppc/context.h>

#include <atomic>
#include <random>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <cstdio>
#include <cstdint>
#include <map>
#include <mutex>
#include <rex/runtime.h>
#include <cstring>

void T6QuickSelectPracticeExit();
void ResetAfterContact();
void SelectAfterContactInput(uint32_t fighter);
void FilterAfterContactButtons(PPCRegister&,PPCRegister&);
bool AfterContactControlsOpponent();
void T6ObserveManualCharacter(PPCRegister&,PPCRegister&,PPCRegister&);
void T6ObserveManualStage(PPCRegister&,PPCRegister&,PPCRegister&);
#define T6_TRANSITION_ENV T6PracticeEnv
#define T6_TRANSITION_ALLOWED() T6AdvancedGuestAvailable()
#include "tekken6_manual_transitions.inc"

thread_local bool redirectPracticePlacement = false;
thread_local bool rightSideCameraRequested = false;
thread_local bool insidePracticeCameraReset = false;
thread_local unsigned cameraUpdatesToTrace = 0;
thread_local std::map<uint32_t, uint32_t> practiceCameraChoices;

// Codegen emits this declaration with a mutable reference; the hook itself
// deliberately leaves the register untouched.
void Tekken6StartupWait(PPCRegister &spin)
{
  static const bool sleep = T6PracticeEnv("TEKKEN6_SLEEP_STARTUP_SPIN") != nullptr;
  static const bool yield = T6PracticeEnv("TEKKEN6_YIELD_STARTUP_SPIN") != nullptr;
  if (spin.s32 > 0)
  {
    if (sleep)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    else if (yield)
    {
      std::this_thread::yield();
    }
  }
  // The guest producer writes this queue flag from another host thread. Keep
  // each iteration's load even when the optional scheduler yield is disabled.
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

// BEGIN FIGHTER INPUT LOOKUP TRACE V1
thread_local uint32_t fighterInputLookupIndex = 0xFFFFFFFFu;
void TraceFighterInputLookup(PPCRegister &index) {
  fighterInputLookupIndex = index.u32;
}
// END FIGHTER INPUT LOOKUP TRACE V1
// BEGIN FIRST PLAYBACK GLOBALS V1
// Test state: 0=not loaded, 1=playing, 2=finished, 3=load failed.
uint32_t firstPlaybackSamples[1800] = {};
unsigned firstPlaybackCount = 0;
unsigned firstPlaybackPosition = 0;
std::atomic<unsigned> firstPlaybackState{0};
std::atomic<uint32_t> firstPlaybackCurrent{0};
std::atomic<unsigned> firstPlaybackReads{0};
thread_local bool firstPlaybackTarget=false;
// END FIRST PLAYBACK GLOBALS V1
// BEGIN OPPONENT CONTROL EXPERIMENT V1
thread_local bool opponentControlSuppressP1 = false;
// END OPPONENT CONTROL EXPERIMENT V1
uint32_t DecodeRecordPlaybackDirection(uint32_t input); // RECORD FACING PLAYBACK V1
void FilterRecordingStart(PPCRegister& caller,PPCRegister& buttons); // RECORD FLOW V1
void TraceTekkenButtons(PPCRegister &object, PPCRegister &buttons)
{
  FilterRecordingStart(object,buttons);
  FilterAfterContactButtons(object,buttons);
  // BEGIN OPPONENT CONTROL RETURN V1
  if (opponentControlSuppressP1 &&
      (object.u32 == 0x822A45CCu || object.u32 == 0x822A45E0u)) {
    buttons.u64 = 0;
  }
  // END OPPONENT CONTROL RETURN V1
  // BEGIN FIRST PLAYBACK RETURN V1
  if (firstPlaybackTarget && firstPlaybackState.load(std::memory_order_acquire)==1 &&
      (object.u32==0x822A45CCu || object.u32==0x822A45E0u)) {
    buttons.u64=DecodeRecordPlaybackDirection(firstPlaybackCurrent.load(std::memory_order_relaxed));
    firstPlaybackReads.fetch_add(1,std::memory_order_relaxed);
  }
  // END FIRST PLAYBACK RETURN V1
  // BEGIN FIGHTER INPUT LOG V1
  if (const char *tracePath = T6PracticeEnv("TEKKEN6_FIGHTER_INPUT_TRACE")) {
    if (*tracePath && object.u32 >= 0x82000000u && object.u32 < 0x83000000u) {
      static std::mutex traceMutex;
      std::lock_guard<std::mutex> lock(traceMutex);
      static std::FILE *file = std::fopen(tracePath, "w");
      static unsigned count = 0;
      static std::map<uint64_t, uint32_t> previous;
      const uint64_t key = (uint64_t(fighterInputLookupIndex) << 32) | object.u32;
      const auto entry = previous.find(key);
      const bool changed = entry == previous.end() || entry->second != buttons.u32;
      if (changed) previous[key] = buttons.u32;
      if (file && changed && count < 20000) {
        std::fprintf(file, "%u index=%u caller=%08X input=%08X\n", ++count,
                     fighterInputLookupIndex, object.u32, buttons.u32);
        std::fflush(file);
      }
    }
  }
  // END FIGHTER INPUT LOG V1
  // BEGIN RECORD POLL TRACE V1
  // Diagnostic only: capture every update for the two observed input objects.
  // These addresses are used only to filter logging, never to alter input.
  if (const char *tracePath = T6PracticeEnv("TEKKEN6_RECORD_POLL_TRACE")) {
    if (*tracePath && (object.u32 == 0x82CDEB3Cu || object.u32 == 0x82CDECCCu)) {
      static std::mutex traceMutex;
      std::lock_guard<std::mutex> traceLock(traceMutex);
      static std::FILE *traceFile = std::fopen(tracePath, "w");
      static unsigned traceCount = 0;
      if (traceFile && traceCount < 20000) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        std::fprintf(traceFile, "%u %lld %08X %08X\n", ++traceCount,
                     static_cast<long long>(micros), object.u32, buttons.u32);
        if ((traceCount % 120) == 0) std::fflush(traceFile);
        if (traceCount == 20000) std::fflush(traceFile);
      }
    }
  }
  // END RECORD POLL TRACE V1
  static const char *path = T6PracticeEnv("TEKKEN6_INPUT_TRACE");
  if (!path || !*path)
    return;

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  static std::FILE *log = std::fopen(path, "w");
  if (!log)
    return;

  static std::map<uint32_t, uint32_t> previous;
  const uint32_t address = object.u32;
  const uint32_t current = buttons.u32;

  auto entry = previous.emplace(address, current);
  if (!entry.second && entry.first->second == current)
    return;

  std::fprintf(log, "object=%08X held=%08X pressed=%08X released=%08X\n",
               static_cast<unsigned>(address),
               static_cast<unsigned>(current),
               static_cast<unsigned>(current & ~entry.first->second),
               static_cast<unsigned>(entry.first->second & ~current));
  entry.first->second = current;
  std::fflush(log);
}

void TraceTekkenStateCommand(PPCRegister &caller, PPCRegister &command)
{
  static const char *path = T6PracticeEnv("TEKKEN6_RESET_TRACE");
  if (!path || !*path)
    return;

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  static std::FILE *log = std::fopen(path, "w");
  if (!log)
    return;

  std::fprintf(log, "caller=%08X command=%u\n",
               static_cast<unsigned>(caller.u32),
               static_cast<unsigned>(command.u32));
  std::fflush(log);
}

namespace
{

  enum class PracticeResetPosition : uint32_t
  {
    Center,
    ReversedCenter,
    LeftWall,
    RightWall
  };

  std::atomic<PracticeResetPosition> preferredResetPosition{
      PracticeResetPosition::Center};

  std::atomic<PracticeResetPosition> pendingResetPosition{
      PracticeResetPosition::Center};

  thread_local PracticeResetPosition activeResetPosition =
      PracticeResetPosition::Center;

  std::atomic<uint32_t> practiceHeld{0};
  std::atomic<bool> preferredRightSideReset{false};
  std::atomic<bool> pendingRightSideReset{false};

  std::atomic<uint32_t> practiceRageOwner{0};
  std::atomic<uint32_t> practiceRageP1{0};
  std::atomic<uint32_t> practiceRageP2{0};

  // Independent Rage settings; false means normal game behavior.
  std::atomic<bool> practiceP1Rage{false};
  std::atomic<bool> practiceP2Rage{false};

  thread_local bool applyRightSideReset = false;
  thread_local uint32_t practicePlayerOne = 0;
}

void LogPracticeSide(
    const char *stage, uint32_t first, uint32_t second)
{
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  static std::FILE *log = std::fopen("reset-side-debug.txt", "w");
  if (!log)
    return;

  std::fprintf(log,
               "%s first=%08X second=%08X p1=%08X apply=%u redirect=%u\n",
               stage,
               static_cast<unsigned>(first),
               static_cast<unsigned>(second),
               static_cast<unsigned>(practicePlayerOne),
               static_cast<unsigned>(applyRightSideReset),
               static_cast<unsigned>(redirectPracticePlacement));
  std::fflush(log);
}

// Host state that must follow a full practice checkpoint.
struct RecordingCycleState {
  bool opponentChord=false,recordChord=false,waitingForRelease=false,playChord=false;
  std::chrono::steady_clock::time_point started{};
} recordingCycleState;
std::mt19937 recordingPlaybackRandom(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
// BEGIN OPPONENT TOGGLE TEST V1
std::atomic<bool> opponentToggleTest{false};
// RECORD SLOTS MENU TEST V1: gameplay updates stop while the practice menu is open.
constexpr unsigned kRecordSlotCount=8; // EIGHT RECORD SLOTS V1
uint32_t recordedSlotSamples[kRecordSlotCount][1800] = {};
unsigned recordedSlotCounts[kRecordSlotCount] = {};
std::atomic<int> selectedRecordSlot{-1};
std::atomic<bool> recordSlotActive{false};
// RECORD ENTRY PLAYBACK V1: mode eligibility is independent of slot rehearsal.
std::atomic<bool> recordPlaybackModeActive{false};
std::atomic<bool> recordSlotRecording{false};
// WEIGHTED PLAYBACK TEST V1
std::atomic<unsigned> recordWeightMenuMode{0};
uint32_t weightedPlaybackSamples[1800]={};
unsigned weightedPlaybackCount=0;
bool weightedPlaybackActive=false;
bool PrepareWeightedPlayback();
#include "tekken6_record_flow.inc" // RECORD FLOW V1
#include "tekken6_record_hud_finish.inc" // RECORD HUD FINISH V1
#include "tekken6_record_facing.inc" // RECORD FACING PLAYBACK V1
// END OPPONENT TOGGLE TEST V1
// RECORD FACING PROBE V1: read-only, throttled gameplay snapshots.
void TraceRecordPlaybackFacing(uint32_t held) {
 const char* enabled=T6PracticeEnv("TEKKEN6_TRACE_RECORD_FACING");
 if(!enabled || std::strcmp(enabled,"1")!=0 || !practiceRageOwner.load(std::memory_order_acquire) ||
    !recordPlaybackModeActive.load(std::memory_order_acquire)) return;
 auto* runtime=rex::Runtime::instance();auto* b=runtime?runtime->virtual_membase():nullptr;if(!b)return;
 auto read=[&](uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];};
 static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);
 static std::FILE* f=std::fopen("record-facing-trace.txt","w");
 static unsigned count=0;static uint32_t previousHeld=0xFFFFFFFFu;
 static auto last=std::chrono::steady_clock::time_point{};
 const auto now=std::chrono::steady_clock::now();
 if(!f || count>=6000 || (held==previousHeld && now-last<std::chrono::milliseconds(100)))return;
 if(!count) {
   std::fprintf(f,"RECORD FACING TRACE V1\ninput-direction-table");
   for(uint32_t i=0;i<18;++i)std::fprintf(f," %08X",read(0x8200EF28u+i*4));
   std::fprintf(f,"\nfields: 04C 050 0C4 0C8 0CC 0D0 0D4 0D8 0DC 0E0 110 114 118 288 28C\n");
 }
 previousHeld=held;last=now;
 std::fprintf(f,"sample=%u held=%08X slot=%d recording=%u playback=%u",++count,held,
   selectedRecordSlot.load(),unsigned(recordSlotRecording.load()),firstPlaybackState.load());
 const uint32_t offsets[]={0x4Cu,0x50u,0xC4u,0xC8u,0xCCu,0xD0u,0xD4u,0xD8u,0xDCu,0xE0u,0x110u,0x114u,0x118u,0x288u,0x28Cu};
 for(uint32_t fighter:{0x82900C00u,0x82901840u}) {
   std::fprintf(f,"\n%08X",fighter);
   for(uint32_t offset:offsets)std::fprintf(f," %08X",read(fighter+offset));
 }
 std::fprintf(f,"\n");std::fflush(f);
}

void CaptureTekkenFrameDataCandidates(uint32_t held);

void CapturePracticeHeld(PPCRegister &held)
{
  practiceHeld.store(held.u32, std::memory_order_relaxed);
  CaptureTekkenFrameDataCandidates(held.u32);
  TraceRecordPlaybackFacing(held.u32);
  // BEGIN RECORD FRAME TRACE V1
  if (const char *path=T6PracticeEnv("TEKKEN6_RECORD_FRAME_TRACE")) {
    if (*path && practiceRageOwner.load(std::memory_order_acquire)) {
      auto *runtime=rex::Runtime::instance();
      uint8_t *base=runtime ? runtime->virtual_membase() : nullptr;
      static std::mutex traceMutex;
      std::lock_guard<std::mutex> lock(traceMutex);
      static std::FILE *f=std::fopen(path,"w");
      static unsigned count=0;
      if (base && f && count<10000) {
        auto readbe=[&](uint32_t a) {return (uint32_t(base[a])<<24)|(uint32_t(base[a+1])<<16)|(uint32_t(base[a+2])<<8)|base[a+3];};
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::fprintf(f,"%u %lld %08X %08X %08X %08X %08X\n",++count,
          static_cast<long long>(us),held.u32,readbe(0x82900C00u+0xC4),readbe(0x82901840u+0xC4),readbe(0x82900C00u+0xC8),readbe(0x82901840u+0xC8));
        if(count%120==0)std::fflush(f);
      }
    }
  }
  // END RECORD FRAME TRACE V1
  // BEGIN OPPONENT TOGGLE INPUT V1
  const char *enabled=T6PracticeEnv("TEKKEN6_TEST_OPPONENT_CONTROL");
  const bool practice=practiceRageOwner.load(std::memory_order_acquire)!=0;
  auto& wasHeld=recordingCycleState.opponentChord;
  if (!enabled || std::strcmp(enabled,"1")!=0 || !practice) {
    wasHeld=false;
    opponentToggleTest.store(false,std::memory_order_relaxed);
  } else {
    const bool heldCombo=(held.u32 & 0x8020u)==0x8020u;
    if (heldCombo && !wasHeld) {
      const bool next=!opponentToggleTest.load(std::memory_order_relaxed);
      opponentToggleTest.store(next,std::memory_order_relaxed);
      static std::FILE *f=std::fopen("opponent-toggle-test.txt","w");
      if(f){std::fprintf(f,"toggle=%s held=%08X\n",next?"on":"off",held.u32);std::fflush(f);}
    }
    wasHeld=heldCombo;
  }
  // END OPPONENT TOGGLE INPUT V1
  // BEGIN RECORD PLAYBACK CYCLE V1
  if (const char *cycle=T6PracticeEnv("TEKKEN6_TEST_RECORD_CYCLE"); cycle && std::strcmp(cycle,"1")==0) {
    // RECORD MODE SWITCH TEST V1: menu switching can stop the recording safely.
    auto& previousCombo=recordingCycleState.recordChord;
    auto& recordWaitingForRelease=recordingCycleState.waitingForRelease; // RECORD START RELEASE V1
    bool recording=recordSlotRecording.load(std::memory_order_acquire);
    if(!recording) recordWaitingForRelease=false;
    auto& started=recordingCycleState.started;
    const char* modeGate=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
    const bool modeRequired=modeGate && std::strcmp(modeGate,"1")==0;
    const bool valid=enabled && std::strcmp(enabled,"1")==0 && practice &&
        (!modeRequired || recordPlaybackModeActive.load(std::memory_order_acquire));
    const bool combo=valid && (held.u32 & 0x4020u)==0x4020u;
    const bool edge=combo && !previousCombo;
    auto log=[](const char *event, unsigned count) {
      if (std::FILE *f=std::fopen("record-cycle-test.txt","a")) {
        std::fprintf(f,"%s samples=%u reads=%u\n",event,count,firstPlaybackReads.load()); std::fclose(f);
      }
    };
    auto finish=[&](const char *reason) {
      recordWaitingForRelease=false;
      recording=false;
      recordSlotRecording.store(false,std::memory_order_release);
      // Retain the native Controller setting until the menu is opened again.
      opponentToggleTest.store(false,std::memory_order_relaxed);
      // Save/open pause only for completed recordings, never teardown.
      if(std::strcmp(reason,"record-stop-start")==0 ||
         std::strcmp(reason,"time-limit")==0 || std::strcmp(reason,"sample-limit")==0) {
        const int slot=selectedRecordSlot.load(std::memory_order_acquire);
        if(slot>=0 && slot<static_cast<int>(kRecordSlotCount) && firstPlaybackCount<=1800) {
          recordedSlotCounts[slot]=firstPlaybackCount;
          std::memcpy(recordedSlotSamples[slot],firstPlaybackSamples,firstPlaybackCount*sizeof(uint32_t));
        }
        if(std::strcmp(reason,"record-stop-start")!=0)
          recordTimeRemainingMillis.store(0,std::memory_order_release);
        recordPauseRequestedOwner.store(practiceRageOwner.load(std::memory_order_acquire),std::memory_order_release);
      }
      log(reason,firstPlaybackCount);
    };
    if (!valid) {
      recordPauseRequestedOwner.store(0,std::memory_order_release); // RECORD HUD FINISH V1
      recordFinishStartRequested.store(false,std::memory_order_release);
      recordFinishStartHeld.store(false,std::memory_order_release);
      // RECORD SLOTS MENU TEST V1: detach when the practice owner goes away.
      if (recording) finish("practice-ended");
      if (const char* slotTest=T6PracticeEnv("TEKKEN6_TEST_RECORD_SLOTS"); slotTest && std::strcmp(slotTest,"1")==0) {
        const int previous=selectedRecordSlot.exchange(-1,std::memory_order_acq_rel);
        if(previous>=0 && previous<static_cast<int>(kRecordSlotCount) && firstPlaybackCount<=1800) {
          recordedSlotCounts[previous]=firstPlaybackCount;
          std::memcpy(recordedSlotSamples[previous],firstPlaybackSamples,firstPlaybackCount*sizeof(uint32_t));
        }
        recordSlotActive.store(false,std::memory_order_release);
     recordPlaybackModeActive.store(false,std::memory_order_release);
      }
      firstPlaybackState.store(0,std::memory_order_release);
      previousCombo=false;
    } else {
      if (recording && !opponentToggleTest.load(std::memory_order_relaxed)) finish("control-ended");
      if (recordFinishStartRequested.exchange(false,std::memory_order_acq_rel) && recording)
        finish("record-stop-start");
      if (edge && !recording && firstPlaybackState.load(std::memory_order_acquire)!=1) {
        if (opponentToggleTest.load(std::memory_order_relaxed)) {
          firstPlaybackState.store(0,std::memory_order_release);
          firstPlaybackCount=0; firstPlaybackPosition=0; recording=true;
          recordWaitingForRelease=true; // Ignore the initiating Select+X until both are released.
          recordTimeRemainingMillis.store(30000,std::memory_order_release);
          recordPauseRequestedOwner.store(0,std::memory_order_release);
          recordSlotRecording.store(true,std::memory_order_release);
          started=std::chrono::steady_clock::now(); log("record-start",0);
        } else if (firstPlaybackCount && !(T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK") && std::strcmp(T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK"),"1")==0)) {
          weightedPlaybackActive=false;
          firstPlaybackPosition=0; firstPlaybackReads.store(0);
          firstPlaybackCurrent.store(0); firstPlaybackState.store(1,std::memory_order_release);
          log("play-start",firstPlaybackCount);
        } else log("no-recording",0);
      }
      previousCombo=combo;
      // BEGIN SLOT PLAYBACK SHORTCUT V1
      // Keep Select+X for recording; Select+B switches from rehearsal to playback.
      auto& previousPlayCombo=recordingCycleState.playChord;
      const char* slotTest=T6PracticeEnv("TEKKEN6_TEST_RECORD_SLOTS");
      const bool playCombo=slotTest && std::strcmp(slotTest,"1")==0 &&
                           (held.u32 & 0x2020u)==0x2020u;
      // WEIGHTED PLAYBACK TEST V1: select without replacing the active slot's recording.
      const char* weightedEnv=T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK");
      const bool weighted=weightedEnv && std::strcmp(weightedEnv,"1")==0;
      // LOOP PLAYBACK TEST V1: a fresh Select+B edge stops active playback.
      const char* loopEnv=T6PracticeEnv("TEKKEN6_TEST_LOOP_PLAYBACK");
      const bool loopEnabled=loopEnv && std::strcmp(loopEnv,"1")==0;
      if (loopEnabled && playCombo && !previousPlayCombo && !recording &&
          firstPlaybackState.load(std::memory_order_acquire)==1) {
        firstPlaybackState.store(2,std::memory_order_release);
        firstPlaybackCurrent.store(0,std::memory_order_relaxed);
        weightedPlaybackActive=false;
        log("play-stopped",firstPlaybackPosition);
      } else if (playCombo && !previousPlayCombo && !recording &&
          (weighted ? recordPlaybackModeActive.load(std::memory_order_acquire) ||
                      (!modeRequired && selectedRecordSlot.load(std::memory_order_acquire)>=0)
                    : selectedRecordSlot.load(std::memory_order_acquire)>=0) &&
          firstPlaybackState.load(std::memory_order_acquire)!=1 &&
          (weighted ? PrepareWeightedPlayback() : firstPlaybackCount>0)) {
        if(!weighted) weightedPlaybackActive=false;
        opponentToggleTest.store(false,std::memory_order_relaxed);
        firstPlaybackPosition=0;
        firstPlaybackReads.store(0,std::memory_order_relaxed);
        firstPlaybackCurrent.store(0,std::memory_order_relaxed);
        firstPlaybackState.store(1,std::memory_order_release);
        log("slot-play-start",firstPlaybackCount);
      }
      previousPlayCombo=playCombo;
      // END SLOT PLAYBACK SHORTCUT V1
      if (recording) {
        const auto elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now()-started).count();
        const unsigned remaining=recordWaitingForRelease?30000u:
          (elapsedMs>=30000?0u:static_cast<unsigned>(30000-elapsedMs));
        recordTimeRemainingMillis.store(remaining,std::memory_order_release);
        if(recordWaitingForRelease) {
          if((held.u32 & 0x4020u)==0) {
            recordWaitingForRelease=false;
            started=std::chrono::steady_clock::now();
            log("record-input-ready",0);
          }
        } else if (std::chrono::steady_clock::now()-started>=std::chrono::seconds(30)) finish("time-limit");
        else if (firstPlaybackCount>=1800) finish("sample-limit");
        else firstPlaybackSamples[firstPlaybackCount++]=EncodeRecordPlaybackDirection((held.u32 & 0x20u)?0:held.u32);
      }
      if (firstPlaybackState.load(std::memory_order_acquire)==1) {
        if (opponentToggleTest.load(std::memory_order_relaxed)) {
          firstPlaybackState.store(0,std::memory_order_release); log("play-cancelled",firstPlaybackPosition);
        } else if (firstPlaybackPosition<(weightedPlaybackActive?weightedPlaybackCount:firstPlaybackCount)) {
          firstPlaybackCurrent.store(weightedPlaybackActive?weightedPlaybackSamples[firstPlaybackPosition++]:firstPlaybackSamples[firstPlaybackPosition++],std::memory_order_relaxed);
        } else if (loopEnabled && (!weighted || PrepareWeightedPlayback())) {
          // Pick again on each pass, retaining the current selected recording.
          firstPlaybackPosition=0;
          firstPlaybackCurrent.store(0,std::memory_order_relaxed);
          log("play-loop",weighted?weightedPlaybackCount:firstPlaybackCount);
        } else {
          firstPlaybackState.store(2,std::memory_order_release); log("play-complete",firstPlaybackPosition);
        }
      }
    }
  } else {
  // BEGIN RECORD CAPTURE TEST V1
  // One temporary in-memory slot; no playback or persistence in this test.
  if (enabled && std::strcmp(enabled,"1")==0 && practice) {
    static uint32_t samples[1800];
    static unsigned count=0;
    static bool recording=false, wasRecordCombo=false;
    static std::chrono::steady_clock::time_point started;
    const bool combo=(held.u32 & 0x4020u)==0x4020u;
    const bool activeControl=opponentToggleTest.load(std::memory_order_relaxed);
    auto save=[&](const char *reason) {
      if (std::FILE *f=std::fopen("recording-first-test.txt","w")) {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::fprintf(f,"reason=%s samples=%u elapsed_ms=%lld\n",reason,count,static_cast<long long>(ms));
        for(unsigned i=0;i<count;++i) std::fprintf(f,"%u %08X\n",i,samples[i]);
        std::fclose(f);
      }
      recording=false;
    };
    if(recording && !activeControl) save("control-ended");
    if(activeControl && combo && !wasRecordCombo) {
      if(recording) save("manual-stop");
      else {count=0;started=std::chrono::steady_clock::now();recording=true;}
    }
    wasRecordCombo=combo;
    if(recording) {
      if(std::chrono::steady_clock::now()-started>=std::chrono::seconds(30)) save("time-limit");
      else if(count>=1800) save("sample-limit");
      else samples[count++]=(held.u32 & 0x20u)?0:held.u32;
    }
  }
  // END RECORD CAPTURE TEST V1
  // BEGIN FIRST PLAYBACK TICK V1
  if (firstPlaybackState.load(std::memory_order_acquire)==1 &&
      !opponentToggleTest.load(std::memory_order_relaxed) && practice) {
    if (firstPlaybackPosition < firstPlaybackCount) {
      firstPlaybackCurrent.store(firstPlaybackSamples[firstPlaybackPosition++],std::memory_order_relaxed);
    } else {
      firstPlaybackState.store(2,std::memory_order_release);
      if (std::FILE *f=std::fopen("first-playback-test.txt","a")) {
        std::fprintf(f,"completed updates=%u getter_reads=%u\n",firstPlaybackPosition,firstPlaybackReads.load());std::fclose(f);
      }
    }
  }
  // END FIRST PLAYBACK TICK V1
  }
  // CYCLE ORIGINAL BASE64: ICAvLyBCRUdJTiBSRUNPUkQgQ0FQVFVSRSBURVNUIFYxCiAgLy8gT25lIHRlbXBvcmFyeSBpbi1tZW1vcnkgc2xvdDsgbm8gcGxheWJhY2sgb3IgcGVyc2lzdGVuY2UgaW4gdGhpcyB0ZXN0LgogIGlmIChlbmFibGVkICYmIHN0ZDo6c3RyY21wKGVuYWJsZWQsIjEiKT09MCAmJiBwcmFjdGljZSkgewogICAgc3RhdGljIHVpbnQzMl90IHNhbXBsZXNbMTgwMF07CiAgICBzdGF0aWMgdW5zaWduZWQgY291bnQ9MDsKICAgIHN0YXRpYyBib29sIHJlY29yZGluZz1mYWxzZSwgd2FzUmVjb3JkQ29tYm89ZmFsc2U7CiAgICBzdGF0aWMgc3RkOjpjaHJvbm86OnN0ZWFkeV9jbG9jazo6dGltZV9wb2ludCBzdGFydGVkOwogICAgY29uc3QgYm9vbCBjb21ibz0oaGVsZC51MzIgJiAweDQwMjB1KT09MHg0MDIwdTsKICAgIGNvbnN0IGJvb2wgYWN0aXZlQ29udHJvbD1vcHBvbmVudFRvZ2dsZVRlc3QubG9hZChzdGQ6Om1lbW9yeV9vcmRlcl9yZWxheGVkKTsKICAgIGF1dG8gc2F2ZT1bJl0oY29uc3QgY2hhciAqcmVhc29uKSB7CiAgICAgIGlmIChzdGQ6OkZJTEUgKmY9c3RkOjpmb3BlbigicmVjb3JkaW5nLWZpcnN0LXRlc3QudHh0IiwidyIpKSB7CiAgICAgICAgY29uc3QgYXV0byBtcz1zdGQ6OmNocm9ubzo6ZHVyYXRpb25fY2FzdDxzdGQ6OmNocm9ubzo6bWlsbGlzZWNvbmRzPihzdGQ6OmNocm9ubzo6c3RlYWR5X2Nsb2NrOjpub3coKS1zdGFydGVkKS5jb3VudCgpOwogICAgICAgIHN0ZDo6ZnByaW50ZihmLCJyZWFzb249JXMgc2FtcGxlcz0ldSBlbGFwc2VkX21zPSVsbGRcbiIscmVhc29uLGNvdW50LHN0YXRpY19jYXN0PGxvbmcgbG9uZz4obXMpKTsKICAgICAgICBmb3IodW5zaWduZWQgaT0wO2k8Y291bnQ7KytpKSBzdGQ6OmZwcmludGYoZiwiJXUgJTA4WFxuIixpLHNhbXBsZXNbaV0pOwogICAgICAgIHN0ZDo6ZmNsb3NlKGYpOwogICAgICB9CiAgICAgIHJlY29yZGluZz1mYWxzZTsKICAgIH07CiAgICBpZihyZWNvcmRpbmcgJiYgIWFjdGl2ZUNvbnRyb2wpIHNhdmUoImNvbnRyb2wtZW5kZWQiKTsKICAgIGlmKGFjdGl2ZUNvbnRyb2wgJiYgY29tYm8gJiYgIXdhc1JlY29yZENvbWJvKSB7CiAgICAgIGlmKHJlY29yZGluZykgc2F2ZSgibWFudWFsLXN0b3AiKTsKICAgICAgZWxzZSB7Y291bnQ9MDtzdGFydGVkPXN0ZDo6Y2hyb25vOjpzdGVhZHlfY2xvY2s6Om5vdygpO3JlY29yZGluZz10cnVlO30KICAgIH0KICAgIHdhc1JlY29yZENvbWJvPWNvbWJvOwogICAgaWYocmVjb3JkaW5nKSB7CiAgICAgIGlmKHN0ZDo6Y2hyb25vOjpzdGVhZHlfY2xvY2s6Om5vdygpLXN0YXJ0ZWQ+PXN0ZDo6Y2hyb25vOjpzZWNvbmRzKDMwKSkgc2F2ZSgidGltZS1saW1pdCIpOwogICAgICBlbHNlIGlmKGNvdW50Pj0xODAwKSBzYXZlKCJzYW1wbGUtbGltaXQiKTsKICAgICAgZWxzZSBzYW1wbGVzW2NvdW50KytdPShoZWxkLnUzMiAmIDB4MjB1KT8wOmhlbGQudTMyOwogICAgfQogIH0KICAvLyBFTkQgUkVDT1JEIENBUFRVUkUgVEVTVCBWMQogIC8vIEJFR0lOIEZJUlNUIFBMQVlCQUNLIFRJQ0sgVjEKICBpZiAoZmlyc3RQbGF5YmFja1N0YXRlLmxvYWQoc3RkOjptZW1vcnlfb3JkZXJfYWNxdWlyZSk9PTEgJiYKICAgICAgIW9wcG9uZW50VG9nZ2xlVGVzdC5sb2FkKHN0ZDo6bWVtb3J5X29yZGVyX3JlbGF4ZWQpICYmIHByYWN0aWNlKSB7CiAgICBpZiAoZmlyc3RQbGF5YmFja1Bvc2l0aW9uIDwgZmlyc3RQbGF5YmFja0NvdW50KSB7CiAgICAgIGZpcnN0UGxheWJhY2tDdXJyZW50LnN0b3JlKGZpcnN0UGxheWJhY2tTYW1wbGVzW2ZpcnN0UGxheWJhY2tQb3NpdGlvbisrXSxzdGQ6Om1lbW9yeV9vcmRlcl9yZWxheGVkKTsKICAgIH0gZWxzZSB7CiAgICAgIGZpcnN0UGxheWJhY2tTdGF0ZS5zdG9yZSgyLHN0ZDo6bWVtb3J5X29yZGVyX3JlbGVhc2UpOwogICAgICBpZiAoc3RkOjpGSUxFICpmPXN0ZDo6Zm9wZW4oImZpcnN0LXBsYXliYWNrLXRlc3QudHh0IiwiYSIpKSB7CiAgICAgICAgc3RkOjpmcHJpbnRmKGYsImNvbXBsZXRlZCB1cGRhdGVzPSV1IGdldHRlcl9yZWFkcz0ldVxuIixmaXJzdFBsYXliYWNrUG9zaXRpb24sZmlyc3RQbGF5YmFja1JlYWRzLmxvYWQoKSk7c3RkOjpmY2xvc2UoZik7CiAgICAgIH0KICAgIH0KICB9CiAgLy8gRU5EIEZJUlNUIFBMQVlCQUNLIFRJQ0sgVjEK
  // END RECORD PLAYBACK CYCLE V1
}

void TracePracticeWallSample(uint32_t held);
void TracePracticeStage();

void CapturePracticeResetPress(PPCRegister &pressed)
{
  constexpr uint32_t up = 0x00000001;
  constexpr uint32_t down = 0x00000002;
  constexpr uint32_t left = 0x00000004;
  constexpr uint32_t right = 0x00000008;
  constexpr uint32_t resetModifier = 0x00000020;
  constexpr uint32_t resetButton = 0x00001000;

  const uint32_t held =
      practiceHeld.load(std::memory_order_relaxed);

  if (!(held & resetModifier) || !(pressed.u32 & resetButton))
    return;

  TracePracticeWallSample(held);
  TracePracticeStage();

  PracticeResetPosition position =
      preferredResetPosition.load(std::memory_order_relaxed);

  if (held & up)
    position = PracticeResetPosition::Center;
  else if (held & down)
    position = PracticeResetPosition::ReversedCenter;
  else if (held & left)
    position = PracticeResetPosition::LeftWall;
  else if (held & right)
    position = PracticeResetPosition::RightWall;

  preferredResetPosition.store(position, std::memory_order_relaxed);
  pendingResetPosition.store(position, std::memory_order_relaxed);

  pendingRightSideReset.store(
      position == PracticeResetPosition::ReversedCenter,
      std::memory_order_relaxed);
}

void RememberPracticePlayerOne(PPCRegister &playerOne)
{
  practicePlayerOne = playerOne.u32;
}

void BeginPracticeSideReset(PPCRegister &command)
{
  ResetAfterContact();

  activeResetPosition =
      pendingResetPosition.exchange(
          PracticeResetPosition::Center,
          std::memory_order_relaxed);

  if (command.u32 != 13)
    activeResetPosition = PracticeResetPosition::Center;

  applyRightSideReset =
      pendingRightSideReset.exchange(false, std::memory_order_relaxed) && command.u32 == 13;

  practicePlayerOne = 0;
  redirectPracticePlacement = false;
  LogPracticeSide("begin", command.u32, 0);

  rightSideCameraRequested = false;
  insidePracticeCameraReset = false;
  practiceCameraChoices.clear();
}

void ApplyPracticeResetSide(
    PPCRegister &first, PPCRegister &second, PPCRegister &command)
{
  redirectPracticePlacement = false;

  if (command.u32 != 13 || !applyRightSideReset)
    return;
  applyRightSideReset = false;

  if (!practicePlayerOne || !first.u32 || !second.u32 || first.u32 == second.u32)
  {
    return;
  }

  // Leave the references unchanged during placement calculations.
  redirectPracticePlacement = first.u32 == practicePlayerOne;
  rightSideCameraRequested = redirectPracticePlacement;
}

uint32_t GetPracticeWallStageId();

void ApplyNohWallResetPositions(PPCRegister &firstPosition)
{
  const PracticeResetPosition position = activeResetPosition;
  activeResetPosition = PracticeResetPosition::Center;

  if (position != PracticeResetPosition::LeftWall &&
      position != PracticeResetPosition::RightWall)
    return;

  struct FighterPosition
  {
    float x;
    float z;
  };

  struct StageWallPositions
  {
    uint32_t stageId;
    FighterPosition leftFirst;
    FighterPosition leftSecond;
    FighterPosition rightFirst;
    FighterPosition rightSecond;
  };

  static constexpr StageWallPositions stages[] = {
      {
          8, // Noh Theater
          {-8498.0107f, -77.1956f},
          {-6527.3008f, -65.8241f},
          {6496.6333f, 30.8987f},
          {8505.2480f, 34.8247f},
      },
      {
          0, // Mystical Forest
          {-8077.3867f, 38.1099f},
          {-6096.9229f, -8.9912f},
          {6440.4243f, 116.9052f},
          {8473.7686f, 153.5904f},
      },
      {
          19, // Anger of the Earth
          {-1.1955f, 10428.2471f},
          {-17.4032f, 8399.5801f},
          {31.7192f, -8325.2930f},
          {104.6564f, -10504.0420f},
      },
      {
          3, // Cemetery
          {5088.2358f, -12064.5273f},
          {4137.2256f, -10324.9238f},
          {1791.5426f, 8474.5166f},
          {2039.5913f, 10519.2480f},
      },
      {
          4, // City After Dark
          {-664.1158f, -8128.4556f},
          {-825.4996f, -6212.9878f},
          {-380.1262f, 6292.2383f},
          {-328.3642f, 8210.9873f},
      },
      {
          18, // Electric Fountain
          {10571.1006f, 58.1530f},
          {8672.0947f, 22.4465f},
          {-8563.9062f, -27.4163f},
          {-10601.4707f, -232.7596f},
      },
      {
          9, // Fallen Colony
          {-2418.2595f, -8225.2461f},
          {-2278.1521f, -6092.1392f},
          {-1702.2510f, 6594.6030f},
          {-1622.6917f, 8510.4668f},
      },
      {
          5, // Fiesta del Tomate
          {-816.4620f, 7057.3799f},
          {-550.8083f, 5277.1592f},
          {1278.0625f, -5344.4478f},
          {1320.5850f, -7281.0742f},
      },
      {
          11, // Gargoyle's Perch
          {2147.0815f, -8089.7998f},
          {1329.4110f, -6566.1538f},
          {3870.8943f, -5527.8096f},
          {4638.7402f, -7261.1738f},
      },
      {
          16, // Lightning Storm
          {-10901.1035f, 1130.6072f},
          {-9034.8545f, 1297.7461f},
          {9256.1182f, 1106.8669f},
          {11241.1230f, 1323.3992f},
      },
      {
          6, // Rustic Asia
          {-7061.9209f, -247.0420f},
          {-5009.4282f, -163.5623f},
          {5154.7217f, -270.0551f},
          {6961.7129f, -106.3547f},
      },
      {
          2, // Temple Grounds
          {7679.5059f, 3773.7380f},
          {5751.4292f, 3743.8127f},
          {-5735.3770f, 3718.1309f},
          {-7787.9219f, 3677.0520f},
      },
      {
          17, // Tunnel Disaster
          {614.2346f, 9065.5762f},
          {677.8888f, 7139.7119f},
          {-1706.4899f, -7248.9634f},
          {-1945.2227f, -9018.3916f},
      },
      {
          10, // Urban War Zone
          {-8958.4941f, 650.6441f},
          {-7220.7788f, 920.0184f},
          {6831.8848f, -286.1549f},
          {8938.0615f, -239.7160f},
      },
  };

  const uint32_t stageId = GetPracticeWallStageId();

  const StageWallPositions *selected = nullptr;
  for (const auto &stage : stages)
  {
    if (stage.stageId == stageId)
    {
      selected = &stage;
      break;
    }
  }

  // Keep the game's calculated placement for unconfigured stages.
  if (!selected)
    return;

  auto *runtime = rex::Runtime::instance();
  if (!runtime || !firstPosition.u32)
    return;

  uint8_t *base = runtime->virtual_membase();
  if (!base)
    return;

  auto writeFloat = [&](uint32_t address, float value)
  {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    uint8_t *p = base + address;
    p[0] = uint8_t(bits >> 24);
    p[1] = uint8_t(bits >> 16);
    p[2] = uint8_t(bits >> 8);
    p[3] = uint8_t(bits);
  };

  const uint32_t first = firstPosition.u32;
  const uint32_t second = first + 16;

  const FighterPosition &firstTarget =
      position == PracticeResetPosition::LeftWall
          ? selected->leftFirst
          : selected->rightFirst;

  const FighterPosition &secondTarget =
      position == PracticeResetPosition::LeftWall
          ? selected->leftSecond
          : selected->rightSecond;

  writeFloat(first, firstTarget.x);
  writeFloat(first + 8, firstTarget.z);
  writeFloat(second, secondTarget.x);
  writeFloat(second + 8, secondTarget.z);
}

void RedirectPracticeFirstPlacement(
    PPCRegister &target,
    PPCRegister &other,
    PPCRegister &firstPosition)
{
  ApplyNohWallResetPositions(firstPosition);

  LogPracticeSide("first-before", target.u32, other.u32);

  if (redirectPracticePlacement)
    target = other;

  LogPracticeSide("first-after", target.u32, other.u32);
}

void RedirectPracticeSecondPlacement(
    PPCRegister &target, PPCRegister &other)
{
  LogPracticeSide("second-before", target.u32, other.u32);

  if (redirectPracticePlacement)
  {
    target = other;
  }

  LogPracticeSide("second-after", target.u32, other.u32);
  redirectPracticePlacement = false;
}

void BeginPracticeCameraReset(PPCRegister &command)
{
  if (command.u32 != 13)
  {
    practiceCameraChoices.clear();
  }

  insidePracticeCameraReset =
      command.u32 == 13 && rightSideCameraRequested;

  cameraUpdatesToTrace = insidePracticeCameraReset ? 24u : 0u;

  LogPracticeSide("camera-begin", command.u32,
                  insidePracticeCameraReset ? 1u : 0u);
}

void SelectPracticeResetCamera(
    PPCRegister &selection, PPCRegister &cameraOutput)
{
  const uint32_t original = selection.u32;
  const uint32_t camera = cameraOutput.u32;

  if (insidePracticeCameraReset && original <= 1)
  {
    // Remember the opposite choice for this reset's camera objects.
    const uint32_t desired = original ^ 1u;
    practiceCameraChoices[camera] = desired;
    selection.u64 = desired;
  }
  else if (original <= 1)
  {
    // Later explicit selections must retain the requested reset side.
    const auto entry = practiceCameraChoices.find(camera);
    if (entry != practiceCameraChoices.end())
    {
      selection.u64 = entry->second;
    }
  }

  // Mode 2 remains automatic, allowing normal camera side transitions.
  if (cameraUpdatesToTrace)
  {
    LogPracticeSide(
        insidePracticeCameraReset
            ? "camera-reset-choice"
            : "camera-followup-choice",
        original, selection.u32);
    --cameraUpdatesToTrace;
  }
}

void EndPracticeCameraReset(PPCRegister &command)
{
  LogPracticeSide("camera-end", command.u32, 0);
  insidePracticeCameraReset = false;
  rightSideCameraRequested = false;
}

void TraceTekkenRage(PPCRegister &fighter, PPCRegister &flags)
{
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  static std::FILE *log = std::fopen("rage-debug.txt", "w");
  if (!log)
    return;

  static std::map<uint32_t, bool> previous;

  const uint32_t address = fighter.u32;
  const bool active = (flags.u32 & 0x800u) != 0;

  const auto entry = previous.emplace(address, active);
  if (!entry.second && entry.first->second == active)
    return;

  entry.first->second = active;

  std::fprintf(log, "fighter=%08X rage=%u flags=%08X\n",
               static_cast<unsigned>(address),
               static_cast<unsigned>(active),
               static_cast<unsigned>(flags.u32));
  std::fflush(log);
}

void BeginPracticeRage(
    PPCRegister &owner,
    PPCRegister &playerOne,
    PPCRegister &playerTwo)
{
  t6_manual_transition::stop();
  practiceRageOwner.store(0, std::memory_order_release);

  const uint32_t p1 = playerOne.u32;
  const uint32_t p2 = playerTwo.u32;

  if (!owner.u32 || !p1 || !p2 || p1 == p2)
    return;

  practiceRageP1.store(p1, std::memory_order_relaxed);
  practiceRageP2.store(p2, std::memory_order_relaxed);
  practiceRageOwner.store(owner.u32, std::memory_order_release);

  LogPracticeSide("rage-practice-enter", p1, p2);
}

void EndPracticeRage(PPCRegister &owner)
{
  T6QuickSelectPracticeExit();
  uint32_t expected = owner.u32;

  if (expected &&
      practiceRageOwner.compare_exchange_strong(
          expected, 0, std::memory_order_acq_rel))
  {
    LogPracticeSide("rage-practice-exit", owner.u32, 0);
  }
}

void ApplyPracticeRageThreshold(
    PPCRegister &fighter,
    PPCRegister &health,
    PPCRegister &threshold)
{
  if (!practiceRageOwner.load(std::memory_order_acquire))
    return;

  bool enabled = false;

  if (fighter.u32 ==
      practiceRageP1.load(std::memory_order_relaxed))
  {
    enabled = practiceP1Rage.load(std::memory_order_relaxed);
  }
  else if (fighter.u32 ==
           practiceRageP2.load(std::memory_order_relaxed))
  {
    enabled = practiceP2Rage.load(std::memory_order_relaxed);
  }

  if (!enabled || health.s32 <= 0)
    return;

  // Make the existing comparison qualify this living fighter for Rage.
  // This changes the comparison threshold, not the fighter's health.
  threshold.u64 = health.u32;
}

void TrackRecordPlaybackWeight(PPCRegister& record, PPCRegister& value);
void TrackRecordPlaybackRage(PPCRegister& record, PPCRegister& value); // RECORD MENU CONTROLS V1

void TracePracticeMenuValue(
    PPCRegister &record, PPCRegister &value)
{
  TrackRecordPlaybackWeight(record,value);
  TrackRecordPlaybackRage(record,value); // RECORD MENU CONTROLS V1
  if (!practiceRageOwner.load(std::memory_order_acquire))
    return;

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  static std::FILE *log =
      std::fopen("practice-menu-debug.txt", "w");
  if (!log)
    return;

  std::fprintf(log, "record=%08X value=%d\n",
               static_cast<unsigned>(record.u32),
               static_cast<int>(value.s32));
  std::fflush(log);
}

void TracePracticeWallSample(uint32_t held)
{
  if (!practiceRageOwner.load(std::memory_order_acquire))
    return;

  const uint32_t p1 =
      practiceRageP1.load(std::memory_order_relaxed);
  const uint32_t p2 =
      practiceRageP2.load(std::memory_order_relaxed);

  auto *runtime = rex::Runtime::instance();
  if (!runtime || !p1 || !p2)
    return;

  const uint8_t *base = runtime->virtual_membase();
  if (!base)
    return;

  auto read32 = [&](uint32_t address) -> uint32_t
  {
    const uint8_t *p = base + address;
    return (uint32_t(p[0]) << 24) |
           (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) |
           uint32_t(p[3]);
  };

  auto readFloat = [&](uint32_t address) -> float
  {
    const uint32_t bits = read32(address);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  };

  const char *sample =
      (held & 0x0004u) ? "left-wall" : (held & 0x0008u) ? "right-wall"
                                   : (held & 0x0001u)   ? "center"
                                                        : "other";

  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  static std::FILE *log =
      std::fopen("wall-reset-samples.txt", "w");
  if (!log)
    return;

  std::fprintf(
      log,
      "%s held=%08X "
      "p1=%08X x=%.4f z=%.4f height=%.4f "
      "p2=%08X x=%.4f z=%.4f height=%.4f\n",
      sample, static_cast<unsigned>(held),
      static_cast<unsigned>(p1),
      double(readFloat(p1)),
      double(readFloat(p1 + 8)),
      double(readFloat(p1 + 72)),
      static_cast<unsigned>(p2),
      double(readFloat(p2)),
      double(readFloat(p2 + 8)),
      double(readFloat(p2 + 72)));
  std::fflush(log);
}

#include "tekken6_practice_rage_menu.inc"
#include "tekken6_expanded_rows.inc"
#include "tekken6_quick_select_trace.inc"
#include "tekken6_stage_load_trace.inc"
#include "tekken6_quick_select_apply.inc"
// WEIGHTED PLAYBACK TEST V1
bool PrepareWeightedPlayback() {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);
 auto* base=memory();const auto records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 const auto c=base?context(base):0;const int selected=selectedRecordSlot.load(std::memory_order_acquire);
 const unsigned mode=recordWeightMenuMode.load(std::memory_order_acquire);
 if(!base||!records||!c||read(base,c)!=0x8207D3E4u||active()||selected>=static_cast<int>(kRecordSlotCount)||mode>=3) return false;
 if(selected<0 && !recordPlaybackModeActive.load(std::memory_order_acquire)) return false;
 unsigned weights[kRecordSlotCount]={},counts[kRecordSlotCount]={},total=0;
 for(unsigned i=0;i<kRecordSlotCount;++i) {
   counts[i]=i==static_cast<unsigned>(selected)?firstPlaybackCount:recordedSlotCounts[i];
   const uint32_t weight=recordPlaybackWeightSettings.values[mode][i];
   if(counts[i]>0 && counts[i]<=1800 && weight<=10) {weights[i]=weight;total+=weight;}
 }
 if(!total) {if(auto* f=std::fopen("weighted-playback-test.txt","a")){std::fprintf(f,"no-eligible mode=%u\n",mode);std::fclose(f);}return false;}
 auto& rng=recordingPlaybackRandom;
 unsigned draw=std::uniform_int_distribution<unsigned>(0,total-1)(rng),chosen=0;
 for(;chosen<kRecordSlotCount;++chosen){if(draw<weights[chosen])break;draw-=weights[chosen];}
 const uint32_t* source=chosen==static_cast<unsigned>(selected)?firstPlaybackSamples:recordedSlotSamples[chosen];
 weightedPlaybackCount=counts[chosen];std::memcpy(weightedPlaybackSamples,source,weightedPlaybackCount*sizeof(uint32_t));weightedPlaybackActive=true;
 if(auto* f=std::fopen("weighted-playback-test.txt","a")) {
   std::fprintf(f,"mode=%u selected=%d played=%u weights=",mode,selected+1,chosen+1);
   for(unsigned i=0;i<kRecordSlotCount;++i) std::fprintf(f,"%s%u",i?",":"",weights[i]);
   std::fprintf(f," samples=%u\n",weightedPlaybackCount);std::fclose(f);
 }
 return true;
}
// BEGIN NATIVE DUMMY CONTROL TEST V1
#include "tekken6_record_opponent_clear.inc" // RECORD OPPONENT CLEAR V1
void NativeDummyControlTest(PPCRegister &selected, PPCRegister &owner) {
  using namespace quick_select_apply_detail;
  const char *enabled=T6PracticeEnv("TEKKEN6_TEST_OPPONENT_CONTROL");
  if (!enabled || std::strcmp(enabled,"1") != 0) return;
  std::lock_guard<std::mutex> lock(mutex);
  uint8_t *b=memory(); if(!b) return;
  const uint32_t c=context(b);
  if (!c || owner.u32!=c || read(b,c)!=0x8207D3E4u || active()) return;
  ObserveRecordedOpponent(b,c); // RECORD OPPONENT CLEAR V1
  // BEGIN FIRST PLAYBACK NATIVE V1
  const char *play=T6PracticeEnv("TEKKEN6_TEST_FIRST_PLAYBACK");
  if (enabled && std::strcmp(enabled,"1")==0 && play && std::strcmp(play,"1")==0 &&
      !opponentToggleTest.load(std::memory_order_relaxed) && firstPlaybackState.load()==0) {
    std::FILE *src=std::fopen("recording-first-test.txt","r");
    unsigned n=0; long long elapsed=0; char reason[80]={}; bool ok=false;
    if(src) {
      if(std::fscanf(src,"reason=%79s samples=%u elapsed_ms=%lld",reason,&n,&elapsed)==3 && n>0 && n<=1800) {
        ok=true; for(unsigned i=0;i<n;++i) {unsigned index=0,value=0; if(std::fscanf(src,"%u %x",&index,&value)!=2 || index!=i){ok=false;break;} firstPlaybackSamples[i]=value;}
      }
      std::fclose(src);
    }
    if (std::FILE *f=std::fopen("first-playback-test.txt","w")) {std::fprintf(f,"load=%s samples=%u\n",ok?"ok":"failed",ok?n:0);std::fclose(f);}
    if(ok){firstPlaybackCount=n;firstPlaybackPosition=0;firstPlaybackCurrent.store(0);firstPlaybackState.store(1,std::memory_order_release);}
    else firstPlaybackState.store(3,std::memory_order_release);
  }
  // END FIRST PLAYBACK NATIVE V1
  if (!opponentToggleTest.load(std::memory_order_relaxed) && firstPlaybackState.load(std::memory_order_acquire)!=1 && !recordSlotActive.load(std::memory_order_acquire) && !recordPlaybackModeActive.load(std::memory_order_acquire)) return;
  static std::FILE *f=std::fopen("native-control-apply.txt","w");
  static unsigned count=0;
  if(f && count++<50) {std::fprintf(f,"owner=%08X native=%u override=7\n",owner.u32,selected.u32);std::fflush(f);}
  selected.u64=7;
}
// END NATIVE DUMMY CONTROL TEST V1

// BEGIN OPPONENT CONTROL IMPLEMENTATION V1
// Deliberately limited to the observed fighter objects and practice context.
void OpponentControlSelectInput(PPCRegister &index, PPCRegister &fighter) {
  SelectAfterContactInput(fighter.u32);
  if(AfterContactControlsOpponent() && fighter.u32==practiceRageP2.load())index.u64=8;
  using namespace quick_select_apply_detail;
  opponentControlSuppressP1 = false;
  // BEGIN FIRST PLAYBACK SELECT V1
  firstPlaybackTarget=false;
  // END FIRST PLAYBACK SELECT V1
  const char *enabled = T6PracticeEnv("TEKKEN6_TEST_OPPONENT_CONTROL");
  if (!enabled || std::strcmp(enabled, "1") != 0 || (!opponentToggleTest.load(std::memory_order_relaxed) && firstPlaybackState.load(std::memory_order_acquire)!=1)) return;
  std::lock_guard<std::mutex> lock(mutex);
  uint8_t *b = memory();
  if (!b) return;
  const uint32_t c = context(b);
  if (!c || read(b,c) != 0x8207D3E4u || active()) return;
  static std::FILE *f = std::fopen("opponent-control-test.txt", "w");
  static unsigned logged = 0;
  if (logged < 50 && (fighter.u32 == 0x82900C00u || fighter.u32 == 0x82901840u)) {
    if (f) { std::fprintf(f,"fighter=%08X index=%u\n",fighter.u32,index.u32); std::fflush(f); }
    ++logged;
  }
  if (fighter.u32 == 0x82900C00u && index.u32 == 8 && opponentToggleTest.load(std::memory_order_relaxed)) opponentControlSuppressP1 = true;
  else if (fighter.u32 == 0x82901840u && index.u32 == 9) {
    index.u64 = 8;
    if (!opponentToggleTest.load(std::memory_order_relaxed) && firstPlaybackState.load(std::memory_order_acquire)==1) firstPlaybackTarget=true;
  }
}
// END OPPONENT CONTROL IMPLEMENTATION V1

uint32_t GetPracticeWallStageId()
{
  using namespace quick_select_apply_detail;

  std::lock_guard<std::mutex> lock(mutex);

  uint8_t *base = memory();
  if (!base)
    return 0xFFFFFFFFu;

  const uint32_t practiceContext = context(base);
  const uint32_t owner =
      practiceRageOwner.load(std::memory_order_acquire);

  if (!practiceContext ||
      practiceContext != owner ||
      read(base, practiceContext) != 0x8207D3E4u)
    return 0xFFFFFFFFu;

  return read(base, practiceContext + 352);
}



#include "tekken6_native_hud_layout.inc" // NATIVE HUD LAYOUT V1






#include "tekken6_practice_prompt_widget.inc" // NATIVE PRACTICE PROMPT FRAME V1

// FRAME DATA HUD V1
#include "tekken6_frame_data_trace.inc"


// Observe validated native selector input; Quick Select keeps its existing path.
void T6ObserveManualCharacter(PPCRegister& manager,PPCRegister& selector,PPCRegister& buttons) {
  using namespace quick_select_apply_detail;
  std::lock_guard<std::mutex> lock(mutex);
  if (active()) return;
  auto* b=memory(); if (!b || !span(manager.u32,344) || !span(selector.u32,212)) return;
  const auto c=context(b); if (!c || read(b,c)!=0x8207D3E4u) return;
  const auto side=read(b,selector.u32+104), state=read(b,selector.u32+96);
  if (side>1 || (state!=2 && state!=5 && state!=16)) return;
  const auto ref=manager.u32+(side?124:100);
  if (read(b,ref)!=selector.u32 || read(b,ref+4)!=read(b,selector.u32+12) ||
      !(read(b,selector.u32+60)&read(b,0x82007560u)) || read(b,selector.u32+120)!=1) return;
  T6AdvancedMarkSelection();
  // Manual selection does not alter audio or vblank pacing.
}
void T6ObserveManualStage(PPCRegister& manager,PPCRegister& selector,PPCRegister& buttons) {
  using namespace quick_select_apply_detail;
  std::lock_guard<std::mutex> lock(mutex);
  if (active()) return;
  auto* b=memory(); if (!b || !span(manager.u32,344) || !span(selector.u32,212)) return;
  const auto c=context(b); if (!c || read(b,c)!=0x8207D3E4u || !read(b,manager.u32+336)) return;
  const auto side=read(b,selector.u32+104); if (side>1) return;
  const auto ref=manager.u32+(side?124:100);
  if (read(b,ref)!=selector.u32 || read(b,ref+4)!=read(b,selector.u32+12) ||
      !(read(b,selector.u32+60)&read(b,0x82007560u))) return;
  T6AdvancedMarkSelection();
  // Manual selection does not alter audio or vblank pacing.
}

// The old practice object is destroyed during an accepted Quick Select reload.
// Do not restore pacing at that intermediate teardown; BeginPracticeRage always
// restores it before the new fight starts. Ordinary exits restore immediately.
void T6QuickSelectPracticeExit() {
  using namespace quick_select_apply_detail;
  std::lock_guard<std::mutex> lock(mutex);
  if (!active()) t6_manual_transition::stop();
}
