#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <mutex>
#include <random>
#include <vector>
#include <sys/mman.h>
// This adapter only uses the context identity; the native backend owns register capture.
struct PPCContext {};
#include "practice/tekken6_frame_data_air_measure.inc"
#include "practice/t6_after_contact.h"
namespace after_contact_detail {t6_after_contact::Runtime state;thread_local bool inputTarget=false;}
unsigned char t6_pagemap[0x100000]{};bool configured=true;
uint8_t* arena=nullptr;PPCContext guest{};PPCContext* g_main_ctx=&guest;bool allowed=true,failLoad=false,incomplete=false;
constexpr uint32_t owner=0xA0100000u,p1=0x82900C00u,p2=0x82901840u;
std::atomic<uint32_t> practiceRageOwner{owner},practiceRageP1{p1},practiceRageP2{p2};
std::atomic<unsigned> firstPlaybackState{0},firstPlaybackCurrent{0},firstPlaybackReads{0},recordWeightMenuMode{0},recordTimeRemainingMillis{30000};
std::atomic<bool> recordSlotActive{false},recordPlaybackModeActive{false},recordSlotRecording{false},opponentToggleTest{false},recordFinishStartRequested{false},recordFinishStartHeld{false};
std::atomic<uint32_t> recordPauseRequestedOwner{0},recordMenuCloseRequestedOwner{0};
std::atomic<int> selectedRecordSlot{-1};
uint32_t firstPlaybackSamples[1800]{},weightedPlaybackSamples[1800]{};
unsigned firstPlaybackCount=0,firstPlaybackPosition=0,weightedPlaybackCount=0;
bool weightedPlaybackActive=false,opponentControlSuppressP1=false,firstPlaybackTarget=false;
struct RecordingCycleState {bool opponentChord=false,recordChord=false,waitingForRelease=false,playChord=false;std::chrono::steady_clock::time_point started{};} recordingCycleState;
std::mt19937 recordingPlaybackRandom{123};
namespace tekken_contacts_live {
std::mutex mutex;tekken_contacts_measure::Meter meter;uint32_t owner=0;
std::atomic<uint64_t> result{0},panels{0};
}
std::map<uint32_t,uint32_t> practiceCameraChoices;bool rightSideCameraRequested=false,insidePracticeCameraReset=false;unsigned cameraUpdatesToTrace=0;
uint64_t rows=1;uint64_t T6RowsGeneration(){return rows;}
bool T6AdvancedGuestAvailable(){return allowed;}
void Lock(int){}uint32_t Objects(uint32_t*,uint32_t){return 0;}
void* Current(){return &guest;}
struct Api {void* (*current_guest_context)();void(*global_lock)(int);uint32_t(*live_guest_objects)(uint32_t*,uint32_t);};
Api api{Current,Lock,Objects};Api& T6Rex(){return api;}
const char* T6Env(const char* name){return configured && (!std::strcmp(name,"T6_SOUND_SEPARATE") || !std::strcmp(name,"T6_SNAP_EXCLUDE_FILE"))?"1":nullptr;}
unsigned beforeAudio=0,afterAudio=0;
void T6VoicesBeforeLoad(){++beforeAudio;}void T6VoicesAfterLoad(){++afterAudio;}
namespace quick_select_apply_detail {
uint8_t* memory(){return arena;}
bool span(uint32_t a,uint32_t n){return !(a&3) && a>=0x82000000u && a<=0xC0000000u-n;}
uint32_t context(const uint8_t*){return owner;}
uint32_t stageIndex(uint32_t id){return id<=20?id:0xFFFFFFFFu;}
uint32_t read(const uint8_t* b,uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];}
void write(uint8_t* b,uint32_t a,uint32_t word){for(int i=3;i>=0;--i){b[a+i]=uint8_t(word);word>>=8;}}
}
// The real module is exercised against a byte-copy snapshot fixture: Windows
// worker/GPU/runtime synchronization remains the existing backend's responsibility.
struct Region{uint32_t addr,size;size_t off;};
unsigned saves=0,loads=0;
namespace t6np {
struct SnapTest {
 bool quiet=false,practice_complete=false;size_t bytes=0;std::vector<Region> snap;std::vector<uint8_t> buffer;
 explicit SnapTest(bool ring){assert(!ring);}
 void Save(){
  assert(practice_complete);++saves;bytes=0;
  for(auto region:{Region{p1,0x800,0},Region{p2,0x800,0},Region{owner,512,0},Region{0x40021000u,4096,0}}){
   if(incomplete && region.addr==0x40021000u)continue;
   region.off=bytes;snap.push_back(region);buffer.insert(buffer.end(),arena+region.addr,arena+region.addr+region.size);bytes+=region.size;
  }
 }
 bool Load() const {
  assert(practice_complete);++loads;if(failLoad)return false;
  for(const auto& region:snap)std::memcpy(arena+region.addr,buffer.data()+region.off,region.size);
  return true;
 }
};
void SavePracticeImage(SnapTest& image){image.practice_complete=true;image.Save();}
bool LoadPracticeImage(const SnapTest& image){return image.Load();}
}
using namespace t6np;
#include "practice/tekken6_position_snapshot.inc"
void Word(uint32_t a,uint32_t v){quick_select_apply_detail::write(arena,a,v);}
uint32_t Word(uint32_t a){return quick_select_apply_detail::read(arena,a);}
void Edge(t6_position_snapshot::Action action){
 uint8_t pad[16]{};T6PositionSnapshotPad(0,pad);pad[5]=0x20;pad[action==t6_position_snapshot::Action::Save?6:7]=255;
 T6PositionSnapshotPad(0,pad);assert(!pad[5] && !pad[6] && !pad[7]);
}
int main(){
 using namespace position_snapshot_detail;using namespace t6_position_snapshot;
 arena=static_cast<uint8_t*>(mmap(nullptr,0x100000000ull,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));assert(arena!=MAP_FAILED);
 Word(0x828B3A10u,0x40021B60u);t6_pagemap[0x40021]=0x41;
 Word(owner,0x8207D3E4u);Word(owner+352,8);Word(owner+356,0);Word(owner+360,1);
 Word(0x82744AA8u,10);Word(0x82744AA8u+108,20);Word(p1+88,10);Word(p2+88,20);
 // Mid-attack and hit-reaction data, beyond position coordinates.
 Word(p1+0x1C0,1234);Word(p1+0xC4,17);Word(p1+0x108,777);Word(p1+0x60,85);
 Word(p2+0x214,12);Word(p2+0x1C4,226);
 Word(p2+0x1C0,5678);Word(p2+0xC4,5);Word(p2+0x108,999);Word(p2+0x60,62);Word(owner+400,31);
 practiceCameraChoices[123]=1;after_contact_detail::state.action.active=true;after_contact_detail::state.action.age=12;
 firstPlaybackCount=10;firstPlaybackPosition=4;firstPlaybackSamples[4]=0x4000;firstPlaybackState=1;
 weightedPlaybackCount=12;weightedPlaybackSamples[5]=0x8000;weightedPlaybackActive=true;
 tekken_contacts_live::meter.pending=true;tekken_contacts_live::meter.startup=17;
 tekken_contacts_live::result=17;tekken_contacts_live::panels=25;
 TickPositionSnapshot(true,true);assert(T6PositionSnapshotAvailable());
 Edge(Action::Load);TickPositionSnapshot(true,true);assert(status==Status::NoSave && !loads);
 // Reject an uninstrumented build and missing launch configuration before capturing.
 t6_pagemap[0x40021]=0;Edge(Action::Save);TickPositionSnapshot(true,true);assert(!checkpoint && status==Status::Invalid && saves==0);
 t6_pagemap[0x40021]=0x41;configured=false;Edge(Action::Save);TickPositionSnapshot(true,true);assert(!checkpoint && status==Status::Invalid && saves==0);configured=true;
 Word(0x40021B60u,0x12345678u);
 Edge(Action::Save);TickPositionSnapshot(true,true);assert(checkpoint && status==Status::Saved && saves==1);
 Word(0x40021B60u,0xDEADBEEFu);
 Word(p1+0x1C0,0x8001);Word(p1+0xC4,77);Word(p1+0x60,5);
 Word(p2+0x214,0);Word(p2+0x1C4,0);
 Word(p2+0x1C0,0x8001);Word(p2+0xC4,88);Word(p2+0x60,2);Word(owner+400,90);
 practiceCameraChoices.clear();after_contact_detail::state.action.age=99;
 firstPlaybackPosition=9;firstPlaybackSamples[4]=0;firstPlaybackState=0;weightedPlaybackSamples[5]=0;
 tekken_contacts_live::meter.pending=false;tekken_contacts_live::result=0;tekken_contacts_live::panels=0;
 Edge(Action::Load);TickPositionSnapshot(true,true);
 assert(status==Status::Loaded && loads==1 && beforeAudio==1 && afterAudio==1);assert(Word(0x40021B60u)==0x12345678u);
 assert(Word(p1+0x1C0)==1234 && Word(p1+0xC4)==17 && Word(p1+0x60)==85);
 assert(Word(p2+0x1C0)==5678 && Word(p2+0xC4)==5 && Word(p2+0x60)==62 && Word(owner+400)==31);
 assert(Word(p2+0x214)==12 && Word(p2+0x1C4)==226);
 assert(firstPlaybackPosition==4 && firstPlaybackSamples[4]==0x4000 && firstPlaybackState==1 && weightedPlaybackSamples[5]==0x8000);
 assert(practiceCameraChoices.at(123)==1);assert(after_contact_detail::state.action.age==12 && after_contact_detail::state.action.active);
 assert(tekken_contacts_live::meter.pending && tekken_contacts_live::meter.startup==17 && tekken_contacts_live::result==17 && tekken_contacts_live::panels==25);
 Edge(Action::Load);TickPositionSnapshot(true,true);assert(status==Status::Loaded && loads==2);
 incomplete=true;Edge(Action::Save);TickPositionSnapshot(true,true);assert(status==Status::Invalid && checkpoint);incomplete=false;
 failLoad=true;Edge(Action::Load);TickPositionSnapshot(true,true);assert(status==Status::Failed && beforeAudio==afterAudio);failLoad=false;
 TickPositionSnapshot(true,false);assert(checkpoint && !T6PositionSnapshotAvailable());TickPositionSnapshot(true,true);
 ++rows;TickPositionSnapshot(true,true);assert(!checkpoint && status==Status::Changed);
 Edge(Action::Load);TickPositionSnapshot(true,true);assert(status==Status::NoSave);
 // Recording countdown resumes from its saved elapsed time rather than wall-clock delay.
 recordSlotRecording=true;recordingCycleState.started=std::chrono::steady_clock::now()-std::chrono::seconds(5);
 Edge(Action::Save);TickPositionSnapshot(true,true);
 recordingCycleState.started=std::chrono::steady_clock::now()-std::chrono::seconds(29);recordSlotRecording=false;
 Edge(Action::Load);TickPositionSnapshot(true,true);
 assert(recordSlotRecording && std::chrono::steady_clock::now()-recordingCycleState.started<std::chrono::seconds(6));
 InvalidatePracticeCheckpoint();TickPositionSnapshot(true,true);assert(!checkpoint && status==Status::Changed);
 Edge(Action::Save);TickPositionSnapshot(true,true);assert(checkpoint);
 Word(owner+352,9);TickPositionSnapshot(true,true);assert(!checkpoint && status==Status::Changed);
 allowed=false;TickPositionSnapshot(false,false);assert(!checkpoint && !T6PositionSnapshotAvailable());
 munmap(arena,0x100000000ull);
 puts("Full-state integration: move/frame/health/timer restore, host playback/meter/countdown rewind, audio pairing and scene guards passed.");
}
