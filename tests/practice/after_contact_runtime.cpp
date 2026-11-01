#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <sys/mman.h>
#include "../../src/practice/tekken6_quick_select_roster.inc"
uint8_t* arena;
constexpr uint32_t owner=0xA0000000u,p1=0x82900C00u,p2=0x82901840u,mode=0xA0001000u,records=0x30010000u;
std::atomic<uint32_t> practiceRageOwner{owner},practiceRageP1{p1},practiceRageP2{p2};
std::atomic<bool> recordPlaybackModeActive{false},opponentToggleTest{false};std::atomic<unsigned> firstPlaybackState{0};
union PPCRegister {uint64_t u64;uint32_t u32;};
namespace practice_rage_menu_detail {std::atomic<uint32_t> menuRecords{records};}
namespace quick_select_apply_detail {
uint8_t* memory(){return arena;}
uint32_t context(uint8_t*){return owner;}
bool span(uint32_t a,unsigned n){return a>=0x82000000u && a<=0xC0000000u-n;}
bool active(){return false;}
uint32_t read(uint8_t* b,uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];}
void write(uint8_t* b,uint32_t a,uint32_t v){for(int i=3;i>=0;--i){b[a+i]=uint8_t(v);v>>=8;}}
}
namespace t6np {struct State;State& state();}
using namespace t6np; // same ambient scope as t6_netplay_internal.h
#include "../../src/practice/tekken6_after_contact.inc"
void Word(uint32_t a,uint32_t v){quick_select_apply_detail::write(arena,a,v);}
uint32_t Word(uint32_t a){return quick_select_apply_detail::read(arena,a);}
void Hit(){Word(p1+0x1AC,1);Word(p1+0x1D0,10);Word(p2+0x1D4,10);Word(p2+0x3CC,1);Word(p2+0x214,12);Word(p2+0x1C4,226);TickAfterContact(true,true);}
void Recover(){Word(p2+0x3CC,0);Word(p2+0x214,0);TickAfterContact(true,true);}
int main(){
 using namespace after_contact_detail;
 arena=static_cast<uint8_t*>(mmap(nullptr,0x100000000ull,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));assert(arena!=MAP_FAILED);
 Word(owner,0x8207D3E4u);Word(owner+388,mode);Word(mode,9);
 Word(nativeDummy,0x400000);Word(nativeDummy+8,p2);Word(nativeDummy+36,1);Word(p2+660,1);Word(p2+0x28C,1);
 Word(records+quick_select_roster::afterContactRow*96+80,t6_after_contact::StandingBlock);
 TickAfterContact(true,true);Hit();
 assert(!after_contact_detail::state.action.waiting && after_contact_detail::state.owned && Word(nativeDummy+36)==7 && Word(p2+660)==0);
 PPCRegister caller{},buttons{};caller.u32=0x822A45CCu;
 SelectAfterContactInput(p1);buttons.u64=0x1000;FilterAfterContactButtons(caller,buttons);assert(buttons.u32==0x1000);
 SelectAfterContactInput(p2);FilterAfterContactButtons(caller,buttons);assert(buttons.u32==8);
 Word(p2+0x28C,0);FilterAfterContactButtons(caller,buttons);assert(buttons.u32==4);
 TickAfterContact(true,false);assert(!after_contact_detail::state.owned && Word(nativeDummy+36)==1 && Word(p2+660)==1);
 // Automatic guard uses native mode 0, never a down-back approximation.
 Word(p2+0x214,0);Word(p2+0x3CC,0);Word(records+quick_select_roster::afterContactRow*96+80,t6_after_contact::BlockAll);
 TickAfterContact(true,true);Hit();assert(after_contact_detail::state.owned && Word(nativeDummy+36)==0 && Word(p2+660)==1);
 SelectAfterContactInput(p2);buttons.u64=123;FilterAfterContactButtons(caller,buttons);assert(buttons.u32==123);
 recordPlaybackModeActive=true;TickAfterContact(true,true);assert(!after_contact_detail::state.owned && Word(nativeDummy+36)==1);
 recordPlaybackModeActive=false;Word(mode,10);TickAfterContact(true,true);assert(!after_contact_detail::state.action.initialized);
 Word(mode,9);Word(nativeDummy+8,p1);TickAfterContact(true,true);assert(!after_contact_detail::state.owned);
 munmap(arena,0x100000000ull);
 puts("Actual runtime adapter: native binding, P2-only input, facing, native automatic guard, pause/recording/CPU gates passed.");
}
