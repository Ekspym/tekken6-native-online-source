// RECORD HUD FINISH V1: UI state and a one-shot request through native pause input.
#pragma once
std::atomic<unsigned> recordTimeRemainingMillis{30000};
std::atomic<uint32_t> recordPauseRequestedOwner{0};
namespace record_hud_finish_detail {
bool LiveOwner(uint32_t owner,uint8_t*& b) {
 auto* runtime=rex::Runtime::instance();b=runtime?runtime->virtual_membase():nullptr;
 return owner && b && owner==practiceRageOwner.load(std::memory_order_acquire) &&
   recordPlaybackModeActive.load(std::memory_order_acquire) &&
   (uint32_t(b[owner])<<24|uint32_t(b[owner+1])<<16|uint32_t(b[owner+2])<<8|b[owner+3])==0x8207D3E4u;
}
uint32_t Read(uint8_t* b,uint32_t a) {
 return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];
}
}
// 8238EFBC is the gameplay pause-opening check, not the menu-closing check.
void RecordFinishPauseInput(PPCRegister& buttons,PPCRegister& owner,PPCRegister& side) {
 const uint32_t pending=recordPauseRequestedOwner.load(std::memory_order_acquire);
 if(!pending)return;
 uint8_t* b=nullptr;
 if(!record_hud_finish_detail::LiveOwner(pending,b)) {
   recordPauseRequestedOwner.store(0,std::memory_order_release);return;
 }
 if(owner.u32!=pending)return;
 const uint32_t controller=record_hud_finish_detail::Read(b,pending+88);
 if(side.u32!=controller)return;
 if(recordPauseRequestedOwner.exchange(0,std::memory_order_acq_rel)!=pending)return;
 // An already-open menu must not receive a synthetic toggle after resume.
 if(record_hud_finish_detail::Read(b,pending+96)!=0)return;
 buttons.u64=buttons.u32|0x10u;
 if(auto* f=std::fopen("record-hud-finish-test.txt","a")) {
   std::fprintf(f,"open-after-record owner=%08X side=%u\n",pending,side.u32);std::fclose(f);
 }
}
// 824631B8 only controls the practice Controller warning dialog. The native
// nonnegative path dismisses that dialog; controller mappings are unchanged.
void HideRecordControllerPrompt(PPCRegister& status) {
 if(AfterContactControlsOpponent()){status.u64=0;return;}
 const uint32_t owner=practiceRageOwner.load(std::memory_order_acquire);
 uint8_t* b=nullptr;
 if(record_hud_finish_detail::LiveOwner(owner,b) &&
    record_hud_finish_detail::Read(b,owner+96)==0)
   status.u64=0;
}
