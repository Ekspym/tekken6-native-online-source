#pragma once
#include "t6_after_contact.h"
namespace after_contact_detail {
t6_after_contact::Runtime state;
thread_local bool inputTarget=false;
constexpr uint32_t nativeDummy=0x827C15C0u;
void Log(const char* event,uint8_t* b){
 if(auto* file=std::fopen("practice-after-contact.txt","a")){
   std::fprintf(file,"%s owner=%08X fighter=%08X choice=%u age=%u native=%u bound=%08X\n",event,
     after_contact_detail::state.owner,after_contact_detail::state.fighter,after_contact_detail::state.action.choice,after_contact_detail::state.action.age,
     b?quick_select_apply_detail::read(b,nativeDummy+36):0,
     b?quick_select_apply_detail::read(b,nativeDummy+8):0);
   std::fclose(file);
 }
}
bool Binding(uint8_t* b){
 using namespace quick_select_apply_detail;
 return b && after_contact_detail::state.owner && context(b)==after_contact_detail::state.owner && read(b,after_contact_detail::state.owner)==0x8207D3E4u &&
   practiceRageP2.load()==after_contact_detail::state.fighter && read(b,nativeDummy+8)==after_contact_detail::state.fighter &&
   (read(b,nativeDummy)&0x400000u);
}
void Release(uint8_t* b){
 using namespace quick_select_apply_detail;
 if(after_contact_detail::state.owned && Binding(b) && read(b,nativeDummy+36)==after_contact_detail::state.written){
   write(b,nativeDummy+36,after_contact_detail::state.mode);
   write(b,after_contact_detail::state.fighter+660,after_contact_detail::state.control);write(b,after_contact_detail::state.fighter+656,after_contact_detail::state.input);
   Log("released",b);
 }
 after_contact_detail::state.owned=false;inputTarget=false;
}
}
void ResetAfterContact(){
 using namespace after_contact_detail;
 Release(quick_select_apply_detail::memory());after_contact_detail::state={};
}
void TickAfterContact(bool allow,bool gameplay){
 using namespace after_contact_detail;
 using namespace quick_select_apply_detail;
 auto* b=memory();const uint32_t c=practiceRageOwner.load(),p1=practiceRageP1.load(),p2=practiceRageP2.load();
 const uint32_t mode=b && span(c,420)?read(b,c+388):0;
 if(!allow || !gameplay || !b || !span(p1,0x800) || !span(p2,0x800) || !span(mode,4) ||
    read(b,mode)!=9 || recordPlaybackModeActive.load() || opponentToggleTest.load() ||
    firstPlaybackState.load()==1 || active()) {ResetAfterContact();return;}
 if(after_contact_detail::state.owner!=c || after_contact_detail::state.fighter!=p2){ResetAfterContact();after_contact_detail::state.owner=c;after_contact_detail::state.fighter=p2;}
 if(!Binding(b)){static unsigned refused=0;if(refused++<8)Log("binding-refused",b);ResetAfterContact();return;}
 const uint32_t records=practice_rage_menu_detail::menuRecords.load();if(!records){ResetAfterContact();return;}
 const unsigned selected=read(b,records+quick_select_roster::afterContactRow*96+80);
 t6_after_contact::Sample sample{read(b,p1+0x1AC),read(b,p1+0x1D0),read(b,p2+0x1D4),read(b,p2+0x60),
   read(b,p2+0x3CC),read(b,p2+0x214),read(b,p2+0xC4),read(b,p2+0x1E8),bool(b[p2+916] && b[p2+917]),read(b,p2+0x1C4)};
 if(!after_contact_detail::state.action.Step(sample,selected)){Release(b);return;}
 if(after_contact_detail::state.action.waiting && after_contact_detail::state.action.wait==1)Log("contact",b);
 if(after_contact_detail::state.action.waiting)return;
 if(!after_contact_detail::state.owned){
   after_contact_detail::state.mode=read(b,nativeDummy+36);after_contact_detail::state.control=read(b,p2+660);after_contact_detail::state.input=read(b,p2+656);after_contact_detail::state.owned=true;
   Log("response-start",b);
 }
 // Native Free Style maps its third built-in choice to mode 0 (automatic guard).
 // Other responses use the same private input route as recording, without
 // changing the user's Opponent Action selector or rerouting P1's controller.
 after_contact_detail::state.written=selected==t6_after_contact::BlockAll?0u:7u;
 write(b,nativeDummy+36,after_contact_detail::state.written);write(b,p2+660,after_contact_detail::state.written==7?0u:1u);
}
void SelectAfterContactInput(uint32_t fighter){
 using namespace after_contact_detail;
 inputTarget=after_contact_detail::state.owned && after_contact_detail::state.action.active &&
   after_contact_detail::state.action.choice!=t6_after_contact::BlockAll && fighter==after_contact_detail::state.fighter;
}
bool AfterContactControlsOpponent(){
 return after_contact_detail::state.owned && after_contact_detail::state.action.active &&
   after_contact_detail::state.action.choice!=t6_after_contact::BlockAll;
}
void FilterAfterContactButtons(PPCRegister& caller,PPCRegister& buttons){
 using namespace after_contact_detail;
 if(!inputTarget || (caller.u32!=0x822A45CCu && caller.u32!=0x822A45E0u))return;
 auto* b=quick_select_apply_detail::memory();if(!Binding(b))return;
 buttons.u64=after_contact_detail::state.action.Buttons(quick_select_apply_detail::read(b,after_contact_detail::state.fighter+0x28C));
}
