#include <cstdint>
#include <cassert>
#include <map>
#include "../../src/practice/tekken6_frame_data_air_rules.inc"
#include "../../src/practice/tekken6_frame_data_air_measure.inc"
int main(){
 std::map<uint32_t,uint32_t> m;
 const uint32_t c=0xA0000000,t=c+0x100,cond=c+0x300,effect=c+0x400,root=c+0x500,pool=c+0x1000;
 auto read=[&](uint32_t a){assert(a>=0x90000000u && a<0xC0000000u);return m[a];};
 m[c+20]=t;m[c+36]=57;m[cond]=0;m[cond+8]=397;
 for(unsigned i=0;i<4;++i){auto r=t+32*i;m[r+4]=0x40000000u|(1u<<i);m[r+8]=cond;m[r+12]=effect;m[r+16]=20;m[r+20]=55;m[r+24]=35;m[r+28]=0x10050;}
 m[t+128]=0x8000;
 using tekken_common_rules::Boundary;
 assert(Boundary(read,c,20,0,root)==35);assert(tekken_common_rules::HasInputs(read,c,20,root));
 assert(!Boundary(read,c,19,0,root));assert(!Boundary(read,c,36,0,root));
 m[t+120]=36;assert(Boundary(read,c,20,0,root)==35);m[t+120]=35;
 m[t+24]=34;assert(!Boundary(read,c,20,0,root));m[t+24]=35;
 m[cond]=153;assert(!Boundary(read,c,20,0,root));m[cond]=0;
 assert(!Boundary(read,c,20,-1,root));assert(!Boundary(read,0,20,0,root));
 // A two-button stance cohort must share windows AND transition effects.
 m[t+68]=0;m[t+100]=0;
 assert(Boundary(read,c,20,0,root)==35);
 m[t+44]=effect+4;assert(!Boundary(read,c,20,0,root));m[t+44]=effect;
 m[t+52]=54;assert(!Boundary(read,c,20,0,root));m[t+52]=55;
 m[t+68]=0x40000004;m[t+100]=0x40000008;
 m[c+20]=pool;m[root+420]=t;m[pool]=0x800B;m[pool+28]=0x50;
 m[t+128]=0x800C;m[pool+32]=0x8000;
 assert(Boundary(read,c,20,0,root)==35);
 m[t]=0x800B;assert(!Boundary(read,c,20,0,root));m[t]=0;
 m[c+20]=0xFFFFFFFCu;assert(tekken_common_rules::HasInputs(read,c,20,root));assert(!Boundary(read,c,20,0,root));
 for(unsigned side=0;side<2;++side)for(bool hit:{false,true}){
   tekken_contacts_measure::Meter meter;tekken_contacts_measure::Sample s{};
   auto& a=s.p[side];auto& d=s.p[1-side];a.action=123;a.animation=15;a.state=0x403;a.attack=1;d.health=180;
   meter.Step(s);a.animation=16;d.pulse=1;d.kind=hit?12:1;d.state=hit?226:94;a.dealt=d.taken=hit?21:0;if(hit)d.health-=21;meter.Step(s);
   a.animation=17;d.animation=1;d.recovery=hit?30:19;d.pulse=0;meter.Step(s);
   for(unsigned j=0;j<100;++j)meter.Step(s);
   a.animation=20;a.attack=0;a.stanceBoundary=35;d.animation=4;meter.Step(s);
   assert(meter.result&(uint64_t(1)<<32));assert(int16_t(meter.result>>16)==(hit?11:0));
 }

 // A transient pulse before kind1 must retain the true contact frame.
 for(bool ambiguous:{false,true}){
   tekken_contacts_measure::Meter meter;tekken_contacts_measure::Sample s{};
   auto& a=s.p[0];auto& d=s.p[1];a.action=5;a.animation=19;a.state=0x400;a.attack=7;a.recovery=40;d.health=100;meter.Step(s);
   a.animation=20;d.pulse=7;d.kind=0;d.state=0x214;a.inputRoutes=ambiguous;meter.Step(s);
   assert(meter.pending && meter.startup==20);
   a.animation=21;d.pulse=0;d.kind=1;d.state=94;d.animation=1;d.recovery=19;a.recoveryGate=1;meter.Step(s);
   assert(meter.startup==20 && meter.pending && !(meter.result&(uint64_t(1)<<32)));
   for(unsigned k=0;k<100;++k)meter.Step(s);
   a.animation=40;d.animation=20;meter.Step(s);
   if(ambiguous)assert(!(meter.result&(uint64_t(1)<<32)));
   else {assert(meter.result&(uint64_t(1)<<32));assert(int16_t(meter.result>>16)==-1);}
 }
}
