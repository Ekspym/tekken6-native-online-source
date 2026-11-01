#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <array>
#include <utility>
#include "../../src/practice/t6_practice_preferences.h"
static std::array<int,t6_practice_preferences::Count> saved;
int T6SavedPracticeSetting(t6_practice_preferences::Setting key){return saved[key];}
void T6SavePracticeSetting(t6_practice_preferences::Setting key,unsigned value){if(value<=unsigned(t6_practice_preferences::maximum[key]))saved[key]=int(value);}
void T6InitializePracticeSettings(const std::array<unsigned,t6_practice_preferences::Count>& values){for(unsigned i=0;i<values.size();++i)if(saved[i]<0)saved[i]=int(values[i]);}
static bool enabled[2]={true,true}, slots=true, recordMode=true, weighted=true;
const char* T6PracticeEnv(const char* name) {
 if(!std::strcmp(name,"TEKKEN6_TEST_RECORD_SLOTS"))return slots?"1":nullptr;
 if(!std::strcmp(name,"TEKKEN6_TEST_RECORD_MODE"))return recordMode?"1":nullptr;
 if(!std::strcmp(name,"TEKKEN6_TEST_WEIGHTED_PLAYBACK"))return weighted?"1":nullptr;
 return nullptr;
}
bool T6FrameDataEnabled(unsigned player){return player<2 && enabled[player];}
void T6SetFrameDataEnabled(unsigned player,bool on){if(player<2){enabled[player]=on;T6SavePracticeSetting(player?t6_practice_preferences::OpponentFrameData:t6_practice_preferences::PlayerFrameData,on?1:0);}}
#include "../../src/practice/tekken6_quick_select_menu_records.inc"
static std::vector<uint8_t> ram(2*1024*1024);
constexpr uint32_t records=4096, texts=400000, stringsStart=800000, owner=512;
union PPCRegister { uint64_t u64; uint32_t u32; };
std::atomic<bool> practiceP1Rage{false},practiceP2Rage{false},recordPlaybackModeActive{true};
namespace practice_rage_menu_detail { std::atomic<uint32_t> menuRecords{records}; }
namespace quick_select_apply_detail {
std::mutex mutex;
using quick_select_menu_detail::read;
using quick_select_menu_detail::write;
uint8_t* memory(){return ram.data();}
uint32_t context(const uint8_t*){return owner;}
bool active(){return false;}
}
#include "../../src/practice/tekken6_record_menu_controls.inc"
void install() {
 using namespace quick_select_menu_detail;
 std::fill(ram.begin(),ram.end(),0);
 for(uint32_t id=0;id<502;++id)write(ram.data(),records+id*96,id);
 const uint32_t freestyle[]={297,300,301,302,311,323,326,329,332,496,499,335,339,342,346,347};
 write(ram.data(),records+296*96+8,16);
 write(ram.data(),records+296*96+16,297);
 for(unsigned i=0;i<16;++i) {
   const uint32_t id=freestyle[i];
   write(ram.data(),records+id*96+4,296);
   write(ram.data(),records+id*96+20,i?freestyle[i-1]:0);
   write(ram.data(),records+id*96+24,i+1<16?freestyle[i+1]:0);
 }
 const uint32_t parents[]={297,350,382};
 const uint32_t starts[]={298,351,383};
 for(unsigned i=0;i<3;++i) {
  write(ram.data(),records+parents[i]*96+16,starts[i]);
  write(ram.data(),records+starts[i]*96+24,starts[i]+1);
  write(ram.data(),records+(starts[i]+1)*96+20,starts[i]);
 }
 write(ram.data(),owner,0x8207D3E4u);
 write(ram.data(),records+302*96+8,8);write(ram.data(),records+302*96+16,303);
 write(ram.data(),records+311*96+8,11);write(ram.data(),records+311*96+16,312);
 uint32_t strings=stringsStart;
 InstallPracticeQuickSelectRecords(ram.data(),records,texts,strings);
 assert(strings-stringsStart<=quick_select_roster::stringBudget);
}
int main() {
 using namespace quick_select_roster;
 using namespace quick_select_menu_detail;
 auto word=[](uint32_t id,uint32_t offset){return read(ram.data(),records+id*96+offset);};
 for(unsigned mask=0;mask<4;++mask) for(bool withSlots:{false,true}) for(bool withWeights:{false,true}) {
  saved.fill(-1);slots=withSlots;weighted=withWeights;enabled[0]=(mask&1)!=0;enabled[1]=(mask&2)!=0;install();
  RestorePracticeSettings();
  for(uint32_t copy=0;copy<frameDataCount;++copy) {
   const unsigned side=copy%2,group=copy/2;
   const bool initial=(mask&(1<<side))!=0;
   if(group>=3 && !slots)continue;
   SyncFrameDataMenu(ram.data(),owner,owner+116,records);
   const uint32_t row=frameDataBase+copy*frameDataStride;
   const uint32_t parent=word(row,4),previous=word(row,20);
   const uint32_t extras=extraSettingsBase+group*extraSettingsStride;
   const uint32_t expectedPrevious=extras+(side?4:1);
   const uint32_t expectedNext=extras+(side?7:4);
   assert(parent==extras);
   assert(previous==expectedPrevious && word(row,24)==expectedNext);
   assert(word(previous,24)==row && word(expectedNext,20)==row);
   assert(word(row,0)==row && word(row,8)==2 && word(row,16)==row+1);
   assert(word(row,80)==unsigned(initial));
   assert(word(row+1,4)==row && word(row+2,4)==row);
   assert(word(row+1,24)==row+2 && word(row+2,20)==row+1);
   unsigned count=0;uint32_t prev=0;
   for(uint32_t id=word(parent,16);id;id=word(id,24)) {
    assert(++count<=6u);
    if(id>=502)assert(word(id,4)==parent);
    if(id>=502)assert(word(id,20)==prev);
    prev=id;
   }
   assert(count==word(parent,8));

   PPCRegister address{},value{};address.u32=records+row*96;value.u32=!initial;
   TrackRecordPlaybackRage(address,value);
   assert(enabled[side]==!initial && enabled[1-side]==((mask&(1<<(1-side)))!=0));
   SyncFrameDataMenu(ram.data(),owner,owner+116,records);
   for(unsigned other=0;other<frameDataCount;++other) {
    const uint32_t id=frameDataBase+other*frameDataStride;
    if(word(id,0)==id)assert(word(id,80)==unsigned(enabled[other%2]));
   }
   PPCRegister object{},action{};object.u32=0x827BF0D0u;action.u32=row;
   RouteRecordPlaybackControlAction(object,action);assert(action.u32==row);
   T6SetFrameDataEnabled(side,initial);
  }
 }
 // Native and recording root lists no longer contain Rage/Frame rows.
 slots=true;weighted=true;recordMode=true;saved.fill(-1);install();RestorePracticeSettings();
 for(uint32_t parent:{296u,recordSlotBase,recordSlotBase+recordSlotStride,recordSlotBase+2*recordSlotStride}) {
   unsigned count=0;
   for(uint32_t id=word(parent,16);id;id=word(id,24)) {
     assert(++count<=(parent==296?15u:14u));
     assert(id!=496 && id!=499);
     assert(id<frameDataBase || id>=frameDataBase+frameDataCount*3);
   }
   assert(count==word(parent,8));
 }
 for(unsigned copy=0;copy<extraSettingsCount;++copy) {
   const uint32_t root=extraSettingsBase+copy*extraSettingsStride;
   const uint32_t parent=word(root,4);unsigned count=0;
   for(uint32_t id=word(parent,16),prev=0;id;prev=id,id=word(id,24)) {
     assert(++count<=5);assert(word(id,20)==prev);
   }
   assert(count==5 && word(parent,8)==5);
   for(unsigned side=0;side<2;++side) {
     PPCRegister address{},value{};address.u32=records+(root+(side?4:1))*96;value.u32=1;
     TrackRecordPlaybackRage(address,value);SyncRecordPlaybackRage(ram.data(),owner,owner+116,records);
     assert(saved[side?t6_practice_preferences::OpponentRage:t6_practice_preferences::PlayerRage]==1);
     assert(word(side?499:496,80)==1);
     for(unsigned other=0;other<extraSettingsCount;++other)assert(word(extraSettingsBase+other*extraSettingsStride+(side?4:1),80)==1);
   }
   for(unsigned offset:{7u,8u}) {
     PPCRegister object{},action{};object.u32=0x827BF0D0u;action.u32=root+offset;
     recordPlaybackModeActive=false;RouteRecordPlaybackControlAction(object,action);
     assert(action.u32==(offset==7?346u:347u));
   }
 }
 for(auto edit:{std::pair<uint32_t,uint32_t>{302,7},{311,10},{364,1}}) {
   PPCRegister address{},value{};address.u32=records+edit.first*96;value.u32=edit.second;
   TrackRecordPlaybackRage(address,value);
 }
 // New native tables/characters start with defaults; restore preferences before native apply.
 install();RestorePracticeSettings();
 assert(word(302,80)==7 && word(311,80)==10);
 for(uint32_t reset:{332u,364u,397u})assert(word(reset,80)==1);
 PPCRegister unrelated{},bad{};unrelated.u32=records+303*96;bad.u32=0;
 const auto expected=saved;TrackRecordPlaybackRage(unrelated,bad);assert(saved==expected);
 unrelated.u32=records+302*96;bad.u32=8;TrackRecordPlaybackRage(unrelated,bad);assert(saved==expected);
 assert(word(302,8)==8 && word(302,16)==303 && word(311,8)==11 && word(311,16)==312);
 assert(word(302,24)==afterContactRow && word(afterContactRow,24)==311 && word(311,20)==afterContactRow);
 assert(word(afterContactRow,8)==10 && word(afterContactRow,16)==afterContactFirst);
 assert(!std::strcmp(reinterpret_cast<const char*>(ram.data()+read(ram.data(),texts+302*40)),"OPPONENT ACTION"));
 assert(!std::strcmp(reinterpret_cast<const char*>(ram.data()+read(ram.data(),texts+311*40)),"GROUND TECHNIQUE"));
 for(unsigned i=0;i<afterContactCount;++i){
   assert(word(afterContactFirst+i,4)==afterContactRow);
   const char* label=reinterpret_cast<const char*>(ram.data()+read(ram.data(),texts+(afterContactFirst+i)*40));
   for(const char* c=label;*c;++c)assert(*c<'a' || *c>'z');
   PPCRegister object{},action{};object.u32=0x827BF0D0u;action.u32=afterContactFirst+i;
   RouteRecordPlaybackControlAction(object,action);assert(action.u32==afterContactFirst+i);
 }
 unrelated.u32=records+afterContactRow*96;bad.u32=9;TrackRecordPlaybackRage(unrelated,bad);
 assert(saved[t6_practice_preferences::AfterContact]==9);
 RestorePracticeSettings();assert(word(afterContactRow,80)==9);
 slots=true;recordMode=false;install();
 for(uint32_t copy=6;copy<frameDataCount;++copy)assert(word(frameDataBase+copy*3,0)==0);
 std::puts("Extra Settings frame toggles: links, independent values and sync passed.");
}
