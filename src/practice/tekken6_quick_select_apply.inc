// Experimental native practice reload. Include after the Rage menu include.
#pragma once
#include <rex/runtime.h>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include "tekken6_quick_select_roster.inc"
namespace quick_select_apply_detail {
std::mutex mutex;
// RECORD WEIGHT DEFAULT FIX V1: native selector setup clears guest values.
// Keep user weights outside guest menu scratch data for this game session.
struct RecordPlaybackWeightSettings {
 uint32_t values[3][quick_select_roster::recordSlotCount];
 RecordPlaybackWeightSettings() {
   for(auto& mode:values) for(auto& weight:mode) weight=10;
 }
} recordPlaybackWeightSettings;

struct Selection { uint32_t p1=0, p2=0, stage=0; } current, pending;
uint32_t syncedContext=0;
unsigned phase=0, characterMask=0;
bool stageSeen=false;
bool nativeLoadReady=false;
bool needsSync=false;
unsigned confirmMask=0;
bool stageConfirmed=false;
std::chrono::steady_clock::time_point started;
bool span(uint32_t a, uint32_t size) {
 return !(a&3) && a>=0x82000000u && size<=0x3E000000u && a<=0xC0000000u-size;
}
uint32_t read(const uint8_t* b,uint32_t a) { return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3]; }
void write(uint8_t* b,uint32_t a,uint32_t v) { for(int i=3;i>=0;--i){b[a+i]=uint8_t(v);v>>=8;} }
uint8_t* memory() { auto* r=rex::Runtime::instance(); return r?r->virtual_membase():nullptr; }
uint32_t context(const uint8_t* b) {
 const uint32_t root=read(b,0x82735C80u), mask=read(b,0x82007560u);
 if(!span(root,912)||read(b,root+12)!=read(b,0x82735C84u)||!(read(b,root+60)&mask))return 0;
 const uint32_t service=read(b,root+908);
 if(!span(service,320))return 0;
 const uint32_t c=read(b,service+312);
 if(!span(c,420)||read(b,c+12)!=read(b,service+316)||!(read(b,c+60)&mask))return 0;
 return c;
}
void log(const char* tag,uint32_t a=0,uint32_t b=0,uint32_t c=0) {
 static std::FILE* f=std::fopen("quick-select-apply-test.txt","w");
 if(f){std::fprintf(f,"%s phase=%u a=%08X b=%u c=%u chars=%u stage=%u\n",tag,phase,a,b,c,characterMask,unsigned(stageSeen));std::fflush(f);}
}
bool active() {
 if(phase && std::chrono::steady_clock::now()-started>std::chrono::seconds(120)) {log("expired");phase=0;t6_manual_transition::stop();}
 return phase!=0;
}
uint32_t index(const uint8_t* b,uint32_t selectedChoice) {
 if(selectedChoice>=102)return 0xFFFFFFFFu;
 const uint32_t fighter=read(b,0x82744AA8u+selectedChoice*108);
 for(uint32_t i=0;i<quick_select_roster::fighterCount;++i) {
  const auto& f=quick_select_roster::fighters[i];
  if(f.id==fighter&&(!f.fixedCostume||f.choice==selectedChoice))return i;
 }
 return 0xFFFFFFFFu;
}
uint32_t stageIndex(uint32_t stage) {
 for(uint32_t i=0;i<quick_select_roster::stageCount;++i)
  if(quick_select_roster::stages[i].id==stage)return i;
 return 0xFFFFFFFFu;
}
uint32_t choice(const uint8_t* b,uint32_t selected,uint32_t old) {
 if(selected>=quick_select_roster::fighterCount||old>=102)return 0xFFFFFFFFu;
 const auto& f=quick_select_roster::fighters[selected];
 if(f.fixedCostume)return f.choice;
 if(index(b,old)==selected)return old;
 const uint32_t variant=read(b,0x82744AA8u+old*108+4);
 return f.choice+(variant==1?1:0);
}
void store(uint8_t* b,uint32_t c,const Selection& s) {write(b,c+356,s.p1);write(b,c+360,s.p2);write(b,c+352,s.stage);}
void completeOnMenu(uint8_t* b,uint32_t c) {
 if(phase!=2||!nativeLoadReady||characterMask!=3||!stageSeen)return;
 if(read(b,c+356)!=pending.p1||read(b,c+360)!=pending.p2||read(b,c+352)!=pending.stage)return;
 if(read(b,0x82900C00u+88)!=read(b,0x82744AA8u+pending.p1*108)||
    read(b,0x82901840u+88)!=read(b,0x82744AA8u+pending.p2*108))return;
 log("apply-complete-on-menu",c,pending.stage);phase=0;syncedContext=0;t6_manual_transition::stop();
}

}
void QuickSelectMenuOpening(PPCRegister& object) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b);
 if(c&&read(b,c)==0x8207D3E4u&&object.u32==c+116){frameDataVisible.store(false,std::memory_order_release);completeOnMenu(b,c);needsSync=true;}
}
// RECORD ENTRY PLAYBACK V2: submenu observation arms the mode; only mode actions release it.
// Called under quick_select_apply_detail::mutex by the native menu update hook.
void ObserveRecordPlaybackMenu(uint8_t* b, uint32_t c, uint32_t object, uint32_t records) {
 using namespace quick_select_apply_detail;
 const char* flag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
 if(!flag || std::strcmp(flag,"1")!=0 || !c || !records ||
    read(b,c)!=0x8207D3E4u || object!=c+116 || active() || read(b,object+4)!=1) return;
 // A native mode action may clear eligibility while its old menu is still on screen.
 // Do not re-arm that same container until an actual navigation edge is observed.
 static uint32_t observedContext=0;
 static int previousContainerMode=-1;
 if(observedContext!=c) {observedContext=c;previousContainerMode=-1;}
 // Native menu offset 44 holds the current container; offset 40 is its selected child.
 uint32_t row=read(b,object+44);
 int mode=-1;
 for(unsigned depth=0;depth<12;++depth) {
   if(row<records || (row-records)%96 || (row-records)/96>=quick_select_roster::totalCount) return;
   const uint32_t id=(row-records)/96;
   if(read(b,row)!=id) return;
   if(id>=quick_select_roster::recordSlotBase &&
      id<quick_select_roster::recordSlotBase+3*quick_select_roster::recordSlotStride &&
      (id-quick_select_roster::recordSlotBase)%quick_select_roster::recordSlotStride==0) {
     mode=static_cast<int>((id-quick_select_roster::recordSlotBase)/quick_select_roster::recordSlotStride);
     break;
   }
   const uint32_t parent=read(b,row+4);
   if(!parent || parent==id) break;
   if(parent>=quick_select_roster::totalCount) return;
   row=records+parent*96;
 }
 const bool entered=mode>=0 && mode!=previousContainerMode;
 previousContainerMode=mode;
 if(mode<0) return;
 recordWeightMenuMode.store(static_cast<unsigned>(mode),std::memory_order_release);
 if(entered && !recordPlaybackModeActive.exchange(true,std::memory_order_acq_rel)) {
   if(auto* f=std::fopen("record-entry-test.txt","a")) {
     std::fprintf(f,"enter-v2 mode=%d selected=%d samples=%u saved=%u,%u,%u\n",
       mode,selectedRecordSlot.load(),firstPlaybackCount,
       recordedSlotCounts[0],recordedSlotCounts[1],recordedSlotCounts[2]);std::fclose(f);
   }
 }
 // Closing or navigating a submenu does not end the selected practice mode.
 // Native mode selections and practice teardown perform the existing save/stop cleanup.
}
// RECORD WEIGHT DEFAULT FIX V1: only actual left/right edits update settings.
void TrackRecordPlaybackWeight(PPCRegister& record, PPCRegister& value) {
 using namespace quick_select_apply_detail;
 const char* flag=T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK");
 if(!flag || std::strcmp(flag,"1")!=0 || value.u32>10) return;
 std::lock_guard<std::mutex> lock(mutex);
 auto* b=memory();if(!b)return;
 const uint32_t c=context(b), records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 if(!c || read(b,c)!=0x8207D3E4u || !records || record.u32<records || (record.u32-records)%96) return;
 const uint32_t id=(record.u32-records)/96;
 if(id<quick_select_roster::recordWeightBase || id>=quick_select_roster::recordModeChoiceBase) return;
 const uint32_t relative=id-quick_select_roster::recordWeightBase;
 const uint32_t mode=relative/quick_select_roster::weightModeSize;
 const uint32_t local=relative%quick_select_roster::weightModeSize;
 if(mode>=3 || local%quick_select_roster::weightGroupSize) return;
 const uint32_t slot=local/quick_select_roster::weightGroupSize;
 if(slot>=quick_select_roster::recordSlotCount || read(b,record.u32)!=id) return;
 recordPlaybackWeightSettings.values[mode][slot]=value.u32;
}
// Called with quick_select_apply_detail::mutex already held.
void SyncRecordPlaybackWeights(uint8_t* b,uint32_t c,uint32_t object,uint32_t records) {
 using namespace quick_select_apply_detail;
 const char* flag=T6PracticeEnv("TEKKEN6_TEST_WEIGHTED_PLAYBACK");
 if(!flag || std::strcmp(flag,"1")!=0 || !c || !records ||
    read(b,c)!=0x8207D3E4u || object!=c+116 || active()) return;
 for(uint32_t mode=0;mode<3;++mode) for(uint32_t slot=0;slot<quick_select_roster::recordSlotCount;++slot) {
   const uint32_t id=quick_select_roster::recordWeightBase+mode*quick_select_roster::weightModeSize+slot*quick_select_roster::weightGroupSize;
   if(read(b,records+id*96)==id)
     write(b,records+id*96+80,recordPlaybackWeightSettings.values[mode][slot]);
 }
}
#include "tekken6_record_menu_controls.inc" // RECORD MENU CONTROLS V1
void QuickSelectSyncMenu(PPCRegister& object) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b), records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 ObserveRecordPlaybackMenu(b,c,object.u32,records);
 SyncRecordPlaybackWeights(b,c,object.u32,records);
 SyncRecordPlaybackRage(b,c,object.u32,records);
 SyncFrameDataMenu(b,c,object.u32,records);
 if(!needsSync)return;
 if(!c||read(b,c)!=0x8207D3E4u||object.u32!=c+116||!records)return;
 if(active())return;
 // BEGIN DUMMY SETTING TRACE V1
 if (const char* path=T6PracticeEnv("TEKKEN6_DUMMY_SETTING_TRACE")) {
  if (*path) {
   static std::FILE* trace=std::fopen(path,"w");
   static unsigned snapshot=0;
   if(trace && snapshot<12) {
    std::fprintf(trace,"SNAPSHOT %u context=%08X records=%08X\n",++snapshot,c,records);
    for(uint32_t i=0;i<496;++i)
     std::fprintf(trace,"record %u value=%u\n",i,read(b,records+i*96+80));
    // BEGIN LIVE DUMMY TRACE V1
    // Read the observed P1 and P2 fighter blocks for comparison only.
    for(uint32_t side=0;side<2;++side) {
     const uint32_t fighter=side?0x82901840u:0x82900C00u;
     for(uint32_t offset=0;offset<0xC00;offset+=4)
      std::fprintf(trace,"fighter %u +%03X = %08X\n",side,offset,read(b,fighter+offset));
    }
    // END LIVE DUMMY TRACE V1
    for(uint32_t offset=0;offset<420;offset+=4)
     std::fprintf(trace,"context +%03X = %08X\n",offset,read(b,c+offset));
    std::fflush(trace);
   }
  }
 }
 // END DUMMY SETTING TRACE V1
 current={read(b,c+356),read(b,c+360),read(b,c+352)};
 if(current.p1>=102||current.p2>=102||current.stage>=100){syncedContext=0;return;}
 const uint32_t p1=index(b,current.p1),p2=index(b,current.p2),stage=stageIndex(current.stage);
 if(p1==0xFFFFFFFFu||p2==0xFFFFFFFFu||stage==0xFFFFFFFFu) {
  syncedContext=0;needsSync=false;log("unsupported-active-selection",c);return;
 }
 for(uint32_t mode=0;mode<3;++mode) {
  const uint32_t delta=mode*quick_select_roster::subtreeSize;
  write(b,records+(503+delta)*96+80,p1);
  write(b,records+(504+delta)*96+80,p2);
  write(b,records+(505+delta)*96+80,stage);
  // RECORD MODE FIX V1: sync the copies used inside RECORD/PLAYBACK.
  const char* modeFlag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
  if(modeFlag && std::strcmp(modeFlag,"1")==0) {
    const uint32_t nested=quick_select_roster::recordQuickBase+mode*quick_select_roster::subtreeSize;
    write(b,records+(nested+1)*96+80,p1);
    write(b,records+(nested+2)*96+80,p2);
    write(b,records+(nested+3)*96+80,stage);
  }
 }
 needsSync=false;syncedContext=c;log("menu-synchronized",c,current.p1,current.p2);log("build-full-roster-v7");
}
void QuickSelectApplyAction(PPCRegister& object,PPCRegister& id) {
 RouteRecordPlaybackControlAction(object,id); // RECORD MENU CONTROLS V1
 using namespace quick_select_apply_detail;
 // BEGIN DEFENSE COMMAND ACTION TRACE V1
 TraceDefenseCommandEvent("apply",object.u32,id.u32,0,0);
 // END DEFENSE COMMAND ACTION TRACE V1
 // RECORD MODE SWITCH TEST V1: handle new mode choices and native mode selections.
 const char* modeTest=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
 if(modeTest && std::strcmp(modeTest,"1")==0) {
   const bool custom=id.u32>=quick_select_roster::recordModeChoiceBase && id.u32<quick_select_roster::recordModeChoiceBase+9;
   const bool native=id.u32==298 || id.u32==299 || id.u32==351 || id.u32==352 || id.u32==383 || id.u32==384 || id.u32==346 || id.u32==347; // RECORD MENU CONTROLS V1
   if(custom||native) {
     std::lock_guard<std::mutex> lock(mutex);uint8_t* b=memory();const uint32_t c=b?context(b):0;
     if(!c || read(b,c)!=0x8207D3E4u || active() || object.u32!=0x827BF0D0u) return;
     const int prev=selectedRecordSlot.load(std::memory_order_acquire);
     if(prev>=0 && prev<static_cast<int>(quick_select_roster::recordSlotCount) && firstPlaybackCount<=1800) {
       recordedSlotCounts[prev]=firstPlaybackCount;
       std::memcpy(recordedSlotSamples[prev],firstPlaybackSamples,firstPlaybackCount*sizeof(uint32_t));
     }
     recordSlotRecording.store(false,std::memory_order_release);
     firstPlaybackState.store(0,std::memory_order_release);
     firstPlaybackCurrent.store(0,std::memory_order_relaxed);
     firstPlaybackPosition=0;weightedPlaybackActive=false;
     selectedRecordSlot.store(-1,std::memory_order_release);
     recordSlotActive.store(false,std::memory_order_release);
     recordPlaybackModeActive.store(false,std::memory_order_release);
     opponentToggleTest.store(false,std::memory_order_release);
     if(custom) {
       const unsigned x=id.u32-quick_select_roster::recordModeChoiceBase, from=x/3, target=x%3;
       // The native list for each mode contains the other two modes.
       // RECORD MODE FIX V1: re-enter the selected native mode even when it matches the underlying mode.
       const uint32_t nativeIds[3][3]={{351,298,299},{351,298,352},{383,384,299}};
       const uint32_t action=nativeIds[from][target];
       if(std::FILE* f=std::fopen("record-mode-test.txt","a")) {
         std::fprintf(f,"from=%u target=%u native=%u saved_slot=%d samples=%u\n",from,target,action,prev,firstPlaybackCount);std::fclose(f);
       }
       // Current-mode choice only exits RECORD/PLAYBACK; other choices use the native callback.
       if(action) id.u64=action;
       return;
     }
     if(std::FILE* f=std::fopen("record-mode-test.txt","a")) {
       std::fprintf(f,"native=%u saved_slot=%d samples=%u\n",id.u32,prev,firstPlaybackCount);std::fclose(f);
     }
   }
 }
 // RECORD SLOTS MENU TEST V1: custom IDs fall through the native switch safely.
 const char* slots=T6PracticeEnv("TEKKEN6_TEST_RECORD_SLOTS");
 const char* controls=T6PracticeEnv("TEKKEN6_TEST_OPPONENT_CONTROL");
 const char* cycle=T6PracticeEnv("TEKKEN6_TEST_RECORD_CYCLE");
 if (slots && std::strcmp(slots,"1")==0 && controls && std::strcmp(controls,"1")==0 &&
     cycle && std::strcmp(cycle,"1")==0 && id.u32>=quick_select_roster::recordSlotBase &&
     id.u32<quick_select_roster::recordSlotBase+3*quick_select_roster::recordSlotStride) {
   const unsigned choice=(id.u32-quick_select_roster::recordSlotBase)%quick_select_roster::recordSlotStride;
   if (!choice || choice==quick_select_roster::recordWeightsOffset) return;
   std::lock_guard<std::mutex> lock(mutex);
   uint8_t* b=memory(); if(!b) return;
   const uint32_t c=context(b);
   if(!c || read(b,c)!=0x8207D3E4u || active() || object.u32!=0x827BF0D0u ||
      recordSlotRecording.load(std::memory_order_acquire)) return;
   const int previous=selectedRecordSlot.load(std::memory_order_acquire);
   if(previous>=0 && previous<static_cast<int>(quick_select_roster::recordSlotCount) && firstPlaybackCount<=1800) {
     recordedSlotCounts[previous]=firstPlaybackCount;
     std::memcpy(recordedSlotSamples[previous],firstPlaybackSamples,firstPlaybackCount*sizeof(uint32_t));
   }
   firstPlaybackState.store(0,std::memory_order_release);
   firstPlaybackCurrent.store(0,std::memory_order_relaxed);
   firstPlaybackPosition=0;
   if(choice==quick_select_roster::recordModeOffset) {
     selectedRecordSlot.store(-1,std::memory_order_release);
     recordSlotActive.store(false,std::memory_order_release);
     recordPlaybackModeActive.store(false,std::memory_order_release);
     opponentToggleTest.store(false,std::memory_order_relaxed);
   } else {
     const int chosen=static_cast<int>(choice-1);
     firstPlaybackCount=recordedSlotCounts[chosen];
     std::memcpy(firstPlaybackSamples,recordedSlotSamples[chosen],firstPlaybackCount*sizeof(uint32_t));
     // WEIGHTED PLAYBACK TEST V1: use the weights from this mode's menu tree.
     recordWeightMenuMode.store((id.u32-quick_select_roster::recordSlotBase)/quick_select_roster::recordSlotStride,std::memory_order_release);
     selectedRecordSlot.store(chosen,std::memory_order_release);
     recordSlotActive.store(true,std::memory_order_release);
     recordPlaybackModeActive.store(true,std::memory_order_release);
     opponentToggleTest.store(true,std::memory_order_relaxed);
     recordMenuCloseRequestedOwner.store(c,std::memory_order_release); // RECORD FLOW V1
   }
   if(std::FILE* f=std::fopen("record-slots-test.txt","a")) {
     std::fprintf(f,"choice=%u previous=%d samples=%u\n",choice,previous,firstPlaybackCount);std::fclose(f);
   }
   return;
 }
 // RECORD MODE FIX V1: accept Apply from the nested QUICK SELECT copy.
 uint32_t mode=0; bool nestedQuick=false;
 if(id.u32==506) mode=0;
 else if(id.u32==506+quick_select_roster::subtreeSize) mode=1;
 else if(id.u32==506+2*quick_select_roster::subtreeSize) mode=2;
 else if(id.u32>=quick_select_roster::recordQuickBase+4 &&
         id.u32<quick_select_roster::recordQuickBase+3*quick_select_roster::subtreeSize &&
         (id.u32-quick_select_roster::recordQuickBase)%quick_select_roster::subtreeSize==4) {
   const char* flag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
   if(!flag || std::strcmp(flag,"1")!=0) return;
   mode=(id.u32-quick_select_roster::recordQuickBase)/quick_select_roster::subtreeSize;nestedQuick=true;
 } else return;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b), records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 if(active()||!c||c!=syncedContext||read(b,c)!=0x8207D3E4u||!records||object.u32!=0x827BF0D0u){log("apply-rejected",c);return;}
 const uint32_t delta=nestedQuick ? quick_select_roster::recordQuickBase+mode*quick_select_roster::subtreeSize-502 : mode*quick_select_roster::subtreeSize;
 const uint32_t p1=read(b,records+(503+delta)*96+80),p2=read(b,records+(504+delta)*96+80),stage=read(b,records+(505+delta)*96+80);
 if(p1>=quick_select_roster::fighterCount||p2>=quick_select_roster::fighterCount||stage>=quick_select_roster::stageCount){log("invalid-selection");return;}
 pending={choice(b,p1,current.p1),choice(b,p2,current.p2),quick_select_roster::stages[stage].id};
 if(pending.p1>=102||pending.p2>=102||pending.stage>=100){log("invalid-choice");return;}
 if(nestedQuick) {
   const int prev=selectedRecordSlot.load(std::memory_order_acquire);
   if(prev>=0 && prev<static_cast<int>(quick_select_roster::recordSlotCount) && firstPlaybackCount<=1800) {
     recordedSlotCounts[prev]=firstPlaybackCount;
     std::memcpy(recordedSlotSamples[prev],firstPlaybackSamples,firstPlaybackCount*sizeof(uint32_t));
   }
   recordSlotRecording.store(false,std::memory_order_release);
   firstPlaybackState.store(0,std::memory_order_release);firstPlaybackCurrent.store(0,std::memory_order_relaxed);
   firstPlaybackPosition=0;weightedPlaybackActive=false;
   selectedRecordSlot.store(-1,std::memory_order_release);recordSlotActive.store(false,std::memory_order_release);
     recordPlaybackModeActive.store(false,std::memory_order_release);
   opponentToggleTest.store(false,std::memory_order_release);
 }
 phase=1;characterMask=0;stageSeen=false;nativeLoadReady=false;confirmMask=0;stageConfirmed=false;started=std::chrono::steady_clock::now();
 t6_manual_transition::begin(); // QUICK SELECT PACING V2
 log("apply-requested",c,pending.p1,pending.p2);log("desired-stage",c,pending.stage);
 // Reuse the live native CHARACTER SELECT callback (including its teardown).
 // This register is the dispatch argument, not the menu record's ID.
 id.u64=mode==0?346:(mode==1?378:411);
}
void QuickSelectRedirectMode(PPCRegister& object,PPCRegister& mode) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);if(!active())return;
 if(phase!=1||mode.u32!=4){log("unexpected-mode-cancel",object.u32,mode.u32);phase=0;t6_manual_transition::stop();return;}
 // Native wrapper 4 owns practice, including its character-selection screens.
 // Keep that wrapper and its complete native selection/setup flow.
 phase=2;log("practice-mode-request",object.u32,mode.u32);
}
// Retain the generated hook name so existing installs need only a rebuild.
// This diagnostic observes transitions; it does not bypass any native state.
void QuickSelectBypassScreen(PPCRegister& state,PPCRegister& target) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);if(!active()||phase!=2)return;
 auto* b=memory();if(!b||state.u32<96||!span(state.u32-96,468))return;
 if(read(b,state.u32-96)!=0x8200B3DCu)return;
 const uint32_t next=uint32_t(target.u64>>32),c=context(b);
 static uint32_t lastObject=0,lastTarget=0;
 if(lastObject!=state.u32||lastTarget!=next) {
  lastObject=state.u32;lastTarget=next;
  log("native-state",state.u32-96,next,c);
 }
 if(next==0x82550298u && c && read(b,c)==0x8207D3E4u) {
  // Native slot164 is reached only after the original selection/setup states.
  // Commit after screen confirmation, before native fighter load requests.
  store(b,c,pending);nativeLoadReady=true;
  log("native-selection-complete",c,pending.p1,pending.p2);
 }
}
void QuickSelectLoadCharacter(PPCRegister& object,PPCRegister& side,PPCRegister& selected) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);if(!active()||phase!=2||!nativeLoadReady||side.u32>1)return;
 auto* b=memory();if(!b)return;const uint32_t c=context(b);if(!c||read(b,c)!=0x8207D3E4u)return;
 const uint32_t desired=side.u32?pending.p2:pending.p1;
 selected.u64=desired;store(b,c,pending);characterMask|=1u<<side.u32;
 log("character-request",object.u32,side.u32,desired);
}
void QuickSelectLoadStage(PPCRegister& object,PPCRegister& selected) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);if(!active()||phase!=2||!nativeLoadReady||selected.u32==0xFFFFFFFFu)return;
 // Keep preview/non-match stage requests separate from the tested final request.
 if(selected.u32!=current.stage&&selected.u32!=pending.stage){log("other-stage-request",object.u32,selected.u32);return;}
 auto* b=memory();if(!b)return;const uint32_t c=context(b);if(!c||read(b,c)!=0x8207D3E4u)return;
 selected.u64=pending.stage;store(b,c,pending);stageSeen=true;log("stage-request",object.u32,pending.stage);
}
void QuickSelectObservePractice(PPCRegister& owner,PPCRegister& p1,PPCRegister& p2) {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);if(!active()||phase!=2)return;
 auto* b=memory();if(!b||context(b)!=owner.u32||!span(p1.u32,92)||!span(p2.u32,92))return;
 const uint32_t a=read(b,p1.u32+88),d=read(b,p2.u32+88);
 log("practice-setup",owner.u32,a,d);
 if(stageSeen&&a==read(b,0x82744AA8u+pending.p1*108)&&d==read(b,0x82744AA8u+pending.p2*108)) {
  log("apply-complete",owner.u32,pending.stage);phase=0;syncedContext=0;t6_manual_transition::stop();
 }
}

// Entry to the native ready-state character confirmation helper. The routine
// still performs eligibility, costume conversion, selection accounting, sound,
// and state advancement. We supply its hover fighter and normal confirm input.
void QuickSelectAutoConfirmCharacter(PPCRegister& manager,PPCRegister& selector,
                                    PPCRegister& buttons,PPCRegister& fighter) {
 T6ObserveManualCharacter(manager,selector,buttons);
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);
 if(!active()||phase!=2||nativeLoadReady)return;
 auto* b=memory();if(!b||!span(manager.u32,344)||!span(selector.u32,212))return;
 const uint32_t c=context(b);if(!c||read(b,c)!=0x8207D3E4u)return;
 const uint32_t side=read(b,selector.u32+104),state=read(b,selector.u32+96);
 if(side>1||(state!=2&&state!=5&&state!=16))return;
 // Verify that this ready selector belongs to the native two-side manager.
 const uint32_t ref=manager.u32+(side?124:100);
 if(read(b,ref)!=selector.u32||read(b,ref+4)!=read(b,selector.u32+12)||
    !(read(b,selector.u32+60)&read(b,0x82007560u)))return;
 // Native state 16 chooses the opposite fighter using the same controller.
 const uint32_t targetSide=state==16?(side^1u):side;
 const uint32_t desired=targetSide?pending.p2:pending.p1;
 const uint32_t variant=read(b,0x82744AA8u+desired*108+4);
 if(variant>1)return; // special costumes stay manual in this focused test
 // A single-fighter practice selection; preserve multi-pick flows.
 const uint32_t picked=read(b,selector.u32+124);
 if(read(b,selector.u32+120)!=1||(state==16?picked>1:picked!=0))return;
 fighter.u64=read(b,0x82744AA8u+desired*108);
 // Native 822F0F88 selects table[0] for bit 0x04000000 (costume0),
 // and table[2] for bit 0x08000000 (costume1). No global controller input changes.
 buttons.u64=variant?0x08000000u:0x04000000u;
 if(!(confirmMask&(1u<<targetSide))) {
  confirmMask|=1u<<targetSide;log("auto-character-confirm",selector.u32,targetSide,desired);
  if(state==16)log("opponent-via-shared-selector",selector.u32,state,picked);
 }
}

// Hook after the native stage candidate is resolved, before its input check.
void QuickSelectAutoConfirmStage(PPCRegister& manager,PPCRegister& selector,
                                PPCRegister& stage,PPCRegister& buttons) {
 T6ObserveManualStage(manager,selector,buttons);
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);
 if(!active()||phase!=2||nativeLoadReady)return;
 auto* b=memory();if(!b||!span(manager.u32,344)||!span(selector.u32,212))return;
 const uint32_t c=context(b);if(!c||read(b,c)!=0x8207D3E4u)return;
 if(read(b,manager.u32+336)==0)return; // native stage-selection input enabled
 const uint32_t side=read(b,selector.u32+104);if(side>1)return;
 const uint32_t ref=manager.u32+(side?124:100);
 if(read(b,ref)!=selector.u32||read(b,ref+4)!=read(b,selector.u32+12)||
    !(read(b,selector.u32+60)&read(b,0x82007560u)))return;
 stage.u64=pending.stage;buttons.u64=0x04000000u;
 if(!stageConfirmed){stageConfirmed=true;log("auto-stage-confirm",manager.u32,pending.stage);}
}

// Build-copy render guard. Native selection updates and state changes still run.
extern "C" bool QuickSelectHideSelectionUI(uint8_t* base,uint32_t object) {
 using namespace quick_select_apply_detail;
 static const bool show=T6PracticeEnv("TEKKEN6_SHOW_QUICK_SELECT_SCREENS")!=nullptr;
 if(show)return false;
 std::lock_guard<std::mutex> lock(mutex);
 if(!active()||phase!=2||!base||!span(object,208))return false;
 const uint32_t c=context(base);
 if(!c||read(base,c)!=0x8207D3E4u||read(base,object)!=0x82012F0Cu)return false;
 // Retain visible manual controls for queued special outfits that v4 cannot
 // auto-confirm. Normal costume0/1 is the supported no-input test path.
 return read(base,0x82744AA8u+pending.p1*108+4)<=1 &&
        read(base,0x82744AA8u+pending.p2*108+4)<=1;
}


void TracePracticeStage()
{
  using namespace quick_select_apply_detail;

  std::lock_guard<std::mutex> lock(mutex);

  uint8_t* base = memory();
  if (!base)
    return;

  const uint32_t practiceContext = context(base);
  const uint32_t owner =
      practiceRageOwner.load(std::memory_order_acquire);

  if (!practiceContext ||
      practiceContext != owner ||
      read(base, practiceContext) != 0x8207D3E4u)
    return;

  const uint32_t stageId = read(base, practiceContext + 352);

  const char* stageName = "unknown";
  for (const auto& stage : quick_select_roster::stages)
  {
    if (stage.id == stageId)
    {
      stageName = stage.name;
      break;
    }
  }

  static std::FILE* log = std::fopen("wall-stage-samples.txt", "w");
  if (!log)
    return;

  std::fprintf(log, "stage=%s id=%u context=%08X\n",
               stageName,
               static_cast<unsigned>(stageId),
               static_cast<unsigned>(practiceContext));
  std::fflush(log);
}

// RECORD FLOW V1: request closure through the game's normal practice close path.
void BeginRecordMenuCloseQuery(PPCRegister& owner) {
 recordMenuCloseQueryOwner=owner.u32;
}
void FinishRecordMenuCloseQuery(PPCRegister& result) {
 using namespace quick_select_apply_detail;
 const uint32_t owner=recordMenuCloseQueryOwner;
 recordMenuCloseQueryOwner=0;
 if(!owner || recordMenuCloseRequestedOwner.load(std::memory_order_acquire)!=owner)return;
 std::lock_guard<std::mutex> lock(mutex);
 auto* b=memory();const uint32_t c=b?context(b):0;
 if(c!=owner || read(b,c)!=0x8207D3E4u || active() ||
    !recordSlotActive.load(std::memory_order_acquire) ||
    !recordPlaybackModeActive.load(std::memory_order_acquire)) {
   recordMenuCloseRequestedOwner.store(0,std::memory_order_release);return;
 }
 if(recordMenuCloseRequestedOwner.exchange(0,std::memory_order_acq_rel)!=owner)return;
 result.u64=1;
 if(auto* f=std::fopen("record-flow-test.txt","a")) {
   std::fprintf(f,"close-after-slot owner=%08X slot=%d\n",owner,selectedRecordSlot.load()+1);std::fclose(f);
 }
}


// RECORD MENU NAVIGATION V1: Back stays inside the Record/Playback root.
// The native back branch at 824365E0 consumes r26; child menus retain Back.
void GuardRecordPlaybackRootBack(PPCRegister& back,PPCRegister& object) {
 using namespace quick_select_apply_detail;
 if(!back.u32 || !recordPlaybackModeActive.load(std::memory_order_acquire))return;
 const char* flag=T6PracticeEnv("TEKKEN6_TEST_RECORD_MODE");
 if(!flag || std::strcmp(flag,"1")!=0)return;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b);
 if(!c || read(b,c)!=0x8207D3E4u || object.u32!=c+116 || active())return;
 const uint32_t container=read(b,object.u32+44);
 if(!span(container,96))return;
 const uint32_t id=read(b,container);
 for(uint32_t mode=0;mode<3;++mode) {
   if(id==quick_select_roster::recordSlotBase+mode*quick_select_roster::recordSlotStride) {
     back.u64=0;return;
   }
 }
}
