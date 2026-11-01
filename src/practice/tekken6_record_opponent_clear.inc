// RECORD OPPONENT CLEAR V1: recordings belong to the opponent character.
#pragma once
namespace record_opponent_clear_detail {
bool known=false;
uint32_t character=0;
}
// Called on the native practice apply thread, under quick_select_apply_detail::mutex,
// after owner/reload validation and before the dummy controller override.
void ObserveRecordedOpponent(uint8_t* b,uint32_t owner) {
 using namespace quick_select_apply_detail;
 const char* slots=T6PracticeEnv("TEKKEN6_TEST_RECORD_SLOTS");
 if(!slots || std::strcmp(slots,"1")!=0)return;
 const uint32_t choice=read(b,owner+360);
 if(choice>=102)return;
 // Resolve selection/costume variants to the game's underlying character ID.
 const uint32_t next=read(b,0x82744AA8u+choice*108);
 bool recognized=false;
 for(const auto& fighter:quick_select_roster::fighters)
   if(fighter.id==next){recognized=true;break;}
 if(!recognized)return;
 using namespace record_opponent_clear_detail;
 if(!known){known=true;character=next;return;}
 if(character==next)return;
 const uint32_t previous=character;character=next;
 firstPlaybackState.store(0,std::memory_order_release);
 firstPlaybackCurrent.store(0,std::memory_order_release);
 firstPlaybackReads.store(0,std::memory_order_relaxed);
 recordSlotRecording.store(false,std::memory_order_release);
 opponentToggleTest.store(false,std::memory_order_release);
 recordSlotActive.store(false,std::memory_order_release);
 selectedRecordSlot.store(-1,std::memory_order_release);
 firstPlaybackCount=0;firstPlaybackPosition=0;
 weightedPlaybackCount=0;weightedPlaybackActive=false;
 std::memset(firstPlaybackSamples,0,sizeof(firstPlaybackSamples));
 std::memset(weightedPlaybackSamples,0,sizeof(weightedPlaybackSamples));
 std::memset(recordedSlotCounts,0,sizeof(recordedSlotCounts));
 std::memset(recordedSlotSamples,0,sizeof(recordedSlotSamples));
 recordFinishStartRequested.store(false,std::memory_order_release);
 recordFinishStartHeld.store(false,std::memory_order_release);
 recordMenuCloseRequestedOwner.store(0,std::memory_order_release);
 recordPauseRequestedOwner.store(0,std::memory_order_release);
 recordTimeRemainingMillis.store(30000,std::memory_order_release);
 // Preserve practice-mode eligibility, weights, Rage and Position Reset settings.
 if(auto* f=std::fopen("record-opponent-change.txt","a")) {
   std::fprintf(f,"opponent=%u -> %u choice=%u cleared=%u_slots\n",
      previous,next,choice,unsigned(kRecordSlotCount));std::fclose(f);
 }
}
