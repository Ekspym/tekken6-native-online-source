// RECORD FLOW V1: slot confirmation closes pause; Start finishes recording.
#pragma once
std::atomic<bool> recordFinishStartRequested{false};
std::atomic<bool> recordFinishStartHeld{false};
std::atomic<uint32_t> recordMenuCloseRequestedOwner{0};
thread_local uint32_t recordMenuCloseQueryOwner=0;
void FilterRecordingStart(PPCRegister& caller,PPCRegister& buttons) {
 const char* cycle=T6PracticeEnv("TEKKEN6_TEST_RECORD_CYCLE");
 if(!cycle || std::strcmp(cycle,"1")!=0 ||
    !recordPlaybackModeActive.load(std::memory_order_acquire))return;
 const uint32_t owner=practiceRageOwner.load(std::memory_order_acquire);
 auto* runtime=rex::Runtime::instance();auto* b=runtime?runtime->virtual_membase():nullptr;
 if(!owner || !b)return;
 auto read=[&](uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];};
 if(read(owner)!=0x8207D3E4u)return;
 const uint32_t controller=read(owner+88)==0?8u:9u;
 if(fighterInputLookupIndex!=controller)return;
 constexpr uint32_t start=0x10u, startRepeat=0x01000000u;
 const bool held=(buttons.u32&start)!=0;
 if(held && recordSlotRecording.load(std::memory_order_acquire)) {
   recordFinishStartRequested.store(true,std::memory_order_release);
   recordFinishStartHeld.store(true,std::memory_order_release);
 }
 if(recordFinishStartHeld.load(std::memory_order_acquire)) {
   buttons.u64=buttons.u32&~(start|startRepeat);
   // Only a held-input read proves release; a zero pressed-edge read does not.
   if(caller.u32==0x824604B0u && !held)
     recordFinishStartHeld.store(false,std::memory_order_release);
 }
}


// RECORD MENU NAVIGATION V1: pressed-edge reads alone cannot prove release.
// At 82268108 r3 is still the native input object; +288 is held buttons.
void ObserveRecordingStartRelease(PPCRegister& input) {
 if(!recordFinishStartHeld.load(std::memory_order_acquire))return;
 const uint32_t owner=practiceRageOwner.load(std::memory_order_acquire);
 auto* runtime=rex::Runtime::instance();auto* b=runtime?runtime->virtual_membase():nullptr;
 if(!owner || !b || !recordPlaybackModeActive.load(std::memory_order_acquire))return;
 auto read=[&](uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];};
 if(read(owner)!=0x8207D3E4u || input.u32<0x82000000u || input.u32>0xBFFFFEDCu)return;
 const uint32_t controller=read(owner+88)==0?8u:9u;
 if(fighterInputLookupIndex!=controller)return;
 if((read(input.u32+288)&0x10u)==0)
   recordFinishStartHeld.store(false,std::memory_order_release);
}
