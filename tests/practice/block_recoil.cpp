#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "../../src/practice/tekken6_frame_data_air_measure.inc"
using namespace tekken_contacts_measure;
static bool valid(const Meter& m){return (m.result&(uint64_t(1)<<32))!=0;}
static void recoil(unsigned side,unsigned kind,unsigned reject){
 Meter m;Sample s{};auto& a=s.p[side];auto& d=s.p[1-side];
 a.frame=a.animation=24;a.attack=15;a.action=10273;a.command=100;a.state=389;a.recovery=54;d.health=180;m.Step(s);
 a.frame=a.animation=25;d.pulse=15;d.kind=kind;d.state=537;d.animation=1;d.recovery=10;m.Step(s);
 assert(m.pending && !valid(m));
 a.frame=a.animation=1;a.attack=0;a.action=2114;a.command=reject==3?100:200;a.state=128;a.recovery=39;
 a.inputRoutes=reject==1?1:0;a.stanceBoundary=2;
 d.pulse=0;d.state=94;d.animation=reject==4?5:1;d.recovery=16;
 if(reject==5)d.health=179;
 m.Step(s);assert(!valid(m));
 for(unsigned i=2;i<=39;++i){
   a.animation=a.frame=i;a.stanceBoundary=i+1;
   a.recoveryGate=i==39 && reject!=2;
   d.animation=i;
   if(i>16){d.kind=0;d.state=128;d.animation=i-16;d.recovery=50;}
   m.Step(s);
   if(i<39)assert(!valid(m));
 }
 if(reject)assert(!valid(m));
 else assert(valid(m) && m.outcome==2 && (m.result&65535)==25 && int16_t(m.result>>16)==-23);
}
static void rising(unsigned side,bool hit){
 Meter m;Sample s{};auto& a=s.p[side];auto& d=s.p[1-side];
 a.frame=a.animation=15;a.attack=23;a.action=2114;a.command=100;a.state=389;a.recovery=47;d.health=180;m.Step(s);
 a.frame=a.animation=16;d.pulse=23;d.kind=hit?12:1;d.state=128;
 if(hit){a.dealt=d.taken=27;d.health=153;}
 m.Step(s);
 a.animation=17;a.frame=1;a.attack=25;a.command=200;a.state=1025;a.dealt=0;
 d.pulse=0;d.animation=1;d.kind=hit?12:1;d.state=hit?226:94;d.recovery=hit?90:19;
 m.Step(s);
 for(unsigned i=18;i<=47;++i){a.animation=i;a.frame=i-16;d.animation=i-16;a.recoveryGate=i==47;m.Step(s);}
 assert((m.result&65535)==16);
 if(hit)assert(!valid(m)); // Do not infer ordinary hit advantage from launch recovery.
 else assert(valid(m) && m.outcome==2 && int16_t(m.result>>16)==-12);
}
int main(){
 for(unsigned side=0;side<2;++side){
   for(unsigned kind:{1u,2u})for(unsigned reject=0;reject<6;++reject)recoil(side,kind,reject);
   rising(side,false);rising(side,true);
 }
}
