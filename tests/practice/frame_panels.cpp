#include "../../src/practice/t6_frame_data_panels.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../src/practice/tekken6_frame_data_air_measure.inc"
uint64_t Contact(unsigned startup,int advantage,bool valid=true) {
 return uint64_t(startup)|(uint64_t(uint16_t(int16_t(advantage)))<<16)|(valid?(uint64_t(1)<<32):0);
}
int main() {
 using namespace t6_frame_panels;
 uint64_t state=Update(0,Contact(10,8),0);
 assert(Startup(state,0)==10 && Startup(state,1)==0);
 assert(Valid(state) && Advantage(state,0)==8 && Advantage(state,1)==-8);
 state=Update(state,Contact(15,1),1);
 assert(Startup(state,0)==10 && Startup(state,1)==15);
 assert(Advantage(state,0)==-1 && Advantage(state,1)==1);
 for(unsigned attacker=0;attacker<2;++attacker)for(int advantage:{-120,-12,-1,0,1,12,120}) {
   state=Update(state,Contact(20,advantage),attacker);
   assert(Valid(state) && Advantage(state,attacker)==advantage);
   assert(Advantage(state,1-attacker)==-advantage);
 }
 state=Update(state,Contact(25,0,false),1);
 assert(!Valid(state) && Startup(state,1)==25 && Startup(state,0)==20);
 assert(Update(state,Contact(1,1),2)==state);
 state=0;assert(!Valid(state) && !Startup(state,0) && !Startup(state,1));
 // A reaction that cannot yield advantage must still carry the contact's owner.
 for(unsigned side=0;side<2;++side) {
   tekken_contacts_measure::Meter meter;tekken_contacts_measure::Sample sample{};
   auto& a=sample.p[side];auto& d=sample.p[1-side];
   a.attack=1;a.animation=9;d.health=180;meter.Step(sample);
   a.animation=10;d.pulse=1;d.kind=3;meter.Step(sample);
   assert(meter.resultAttacker==side && meter.result==10 && meter.outcome==3);
 }
 puts("Per-player startup and mirrored signed advantage checks passed.");
}
