// Extra Settings selectors share canonical values with native practice.
#pragma once
#include "t6_practice_preferences.h"
// Called with the Quick Select mutex held. Native settings remain the source
// consumed by the game's practice initializer; no fighter memory is persisted.
void RestorePracticeSettingsValues(uint8_t* b,uint32_t records) {
 using namespace quick_select_apply_detail;
 using namespace t6_practice_preferences;
 if(!b || !records)return;
 for(auto entry:{std::pair<uint32_t,Setting>{302,Dummy1},{311,Dummy2},{496,PlayerRage},{499,OpponentRage}}) {
   const int saved=T6SavedPracticeSetting(entry.second);
   if(saved>=0 && saved<=maximum[entry.second])write(b,records+entry.first*96+80,unsigned(saved));
 }
 const int reset=T6SavedPracticeSetting(PositionReset);
 const int response=T6SavedPracticeSetting(AfterContact);
 if(response>=0 && response<=9)write(b,records+quick_select_roster::afterContactRow*96+80,unsigned(response));
 if(reset>=0 && reset<=1)for(uint32_t id:{332u,364u,397u})write(b,records+id*96+80,unsigned(reset));
 practiceP1Rage.store(read(b,records+496*96+80)==1,std::memory_order_relaxed);
 practiceP2Rage.store(read(b,records+499*96+80)==1,std::memory_order_relaxed);
}
void RestorePracticeSettings() {
 using namespace quick_select_apply_detail;
 std::lock_guard<std::mutex> lock(mutex);
 auto* b=memory();const uint32_t records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 if(!b || !records)return;
 T6InitializePracticeSettings({read(b,records+302*96+80),read(b,records+311*96+80),
   read(b,records+496*96+80),read(b,records+499*96+80),read(b,records+332*96+80),
   T6FrameDataEnabled(0)?1u:0u,T6FrameDataEnabled(1)?1u:0u,0u});
 RestorePracticeSettingsValues(b,records);
}
void SyncFrameDataMenu(uint8_t* b,uint32_t c,uint32_t object,uint32_t records) {
 using namespace quick_select_apply_detail;
 if(!records || !c || read(b,c)!=0x8207D3E4u || object!=c+116 || active())return;
 for(uint32_t copy=0;copy<quick_select_roster::frameDataCount;++copy) {
   const uint32_t id=quick_select_roster::frameDataBase+copy*quick_select_roster::frameDataStride;
   if(read(b,records+id*96)==id)write(b,records+id*96+80,T6FrameDataEnabled(copy%2)?1:0);
 }
}
void SyncRecordPlaybackRage(uint8_t* b,uint32_t c,uint32_t object,uint32_t records) {
 using namespace quick_select_apply_detail;
 if(!records || !c || read(b,c)!=0x8207D3E4u || object!=c+116 || active())return;
 RestorePracticeSettingsValues(b,records);
 for(uint32_t mode=0;mode<3;++mode) {
   const uint32_t first=quick_select_roster::recordExtrasBase+mode*quick_select_roster::recordExtrasStride;
   const uint32_t nativeResetRows[]={332,364,397};
   if(read(b,records+first*96)==first)write(b,records+first*96+80,read(b,records+nativeResetRows[mode]*96+80));
 }
 for(uint32_t copy=0;copy<quick_select_roster::extraSettingsCount;++copy) {
   const uint32_t root=quick_select_roster::extraSettingsBase+copy*quick_select_roster::extraSettingsStride;
   if(read(b,records+root*96)!=root)continue;
   for(uint32_t side=0;side<2;++side)
     write(b,records+(root+(side?4:1))*96+80,read(b,records+(side?499:496)*96+80));
 }
}
void TrackRecordPlaybackRage(PPCRegister& record,PPCRegister& value) {
 using namespace quick_select_apply_detail;
 using namespace t6_practice_preferences;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b),records=practice_rage_menu_detail::menuRecords.load(std::memory_order_acquire);
 if(!c || !records || read(b,c)!=0x8207D3E4u || active() || record.u32<records || (record.u32-records)%96)return;
 const uint32_t id=(record.u32-records)/96;
 if(id>=quick_select_roster::totalCount || read(b,record.u32)!=id)return;
 if(id==quick_select_roster::afterContactRow) {
   if(value.u32<quick_select_roster::afterContactCount)T6SavePracticeSetting(AfterContact,value.u32);
   return;
 }
 if(id==302 || id==311) {
   const Setting setting=id==302?Dummy1:Dummy2;
   if(value.u32<=unsigned(maximum[setting]))T6SavePracticeSetting(setting,value.u32);
   return;
 }
 if(value.u32>1)return;
 for(uint32_t copy=0;copy<quick_select_roster::frameDataCount;++copy) {
   if(id==quick_select_roster::frameDataBase+copy*quick_select_roster::frameDataStride) {
     T6SetFrameDataEnabled(copy%2,value.u32==1);return;
   }
 }
 bool reset=id==332 || id==364 || id==397;
 for(uint32_t mode=0;mode<3;++mode)
   reset=reset || id==quick_select_roster::recordExtrasBase+mode*quick_select_roster::recordExtrasStride;
 if(reset) {
   for(uint32_t native:{332u,364u,397u})write(b,records+native*96+80,value.u32);
   T6SavePracticeSetting(PositionReset,value.u32);return;
 }
 for(uint32_t side=0;side<2;++side) {
   bool rage=id==(side?499u:496u);
   for(uint32_t copy=0;copy<quick_select_roster::extraSettingsCount;++copy)
     rage=rage || id==quick_select_roster::extraSettingsBase+copy*quick_select_roster::extraSettingsStride+(side?4:1);
   if(!rage)continue;
   write(b,records+(side?499:496)*96+80,value.u32);
   if(side)practiceP2Rage.store(value.u32==1,std::memory_order_relaxed);
   else practiceP1Rage.store(value.u32==1,std::memory_order_relaxed);
   T6SavePracticeSetting(side?OpponentRage:PlayerRage,value.u32);return;
 }
}
void RouteRecordPlaybackControlAction(PPCRegister& object,PPCRegister& id) {
 using namespace quick_select_apply_detail;
 bool extra=id.u32>=quick_select_roster::extraSettingsBase &&
   id.u32<quick_select_roster::extraSettingsBase+quick_select_roster::extraSettingsCount*quick_select_roster::extraSettingsStride;
 bool record=id.u32>=quick_select_roster::recordExtrasBase &&
   id.u32<quick_select_roster::recordExtrasBase+3*quick_select_roster::recordExtrasStride;
 if(!extra && !record)return;
 const uint32_t choice=extra?(id.u32-quick_select_roster::extraSettingsBase)%quick_select_roster::extraSettingsStride:
   (id.u32-quick_select_roster::recordExtrasBase)%quick_select_roster::recordExtrasStride;
 if(choice!=7 && choice!=8)return;
 std::lock_guard<std::mutex> lock(mutex);auto* b=memory();if(!b)return;
 const uint32_t c=context(b);
 if(!c || read(b,c)!=0x8207D3E4u || active() || object.u32!=0x827BF0D0u ||
    (!extra && !recordPlaybackModeActive.load(std::memory_order_acquire)))return;
 // Reuse native confirmation and teardown for both exit actions. The normal
 // mode-action cleanup also stops recording/playback without deleting slots.
 id.u64=choice==7?346u:347u;
}
