#include <cassert>
#include <cstdint>
#include "../../src/practice/tekken6_frame_data_air_measure.inc"
using namespace tekken_contacts_measure;
static bool valid(const Meter& m) { return (m.result & (uint64_t(1)<<32))!=0; }
static void guard(unsigned side,unsigned kind) {
 Meter m; Sample s{}; auto& a=s.p[side];auto& d=s.p[1-side];
 a.frame=a.animation=22;a.attack=10;a.action=10273;a.recovery=60;d.health=180;m.Step(s);
 a.frame=a.animation=23;d.kind=kind;d.state=kind==2?537:532;d.pulse=10;d.animation=1;d.recovery=10;m.Step(s);
 assert(m.pending && m.pendingType==2 && !valid(m));
 a.frame=a.animation=24;d.state=94;d.pulse=0;d.animation=1;d.recovery=19;m.Step(s);
 assert(valid(m) && m.outcome==2 && (m.result&65535)==23 && int16_t(m.result>>16)==-18);
}
// Reproduce the automatic recovery-phase handover in the supplied capture.
static void recovery(unsigned side,unsigned reject) {
 Meter m;Sample s{};auto& a=s.p[side];auto& d=s.p[1-side];
 a.frame=a.animation=18;a.attack=12;a.action=10273;a.command=100;a.state=389;a.recovery=51;d.health=180;m.Step(s);
 a.frame=a.animation=19;a.dealt=d.taken=14;d.kind=12;d.state=128;d.health=166;d.pulse=12;m.Step(s);
 assert(m.pending && !valid(m));
 a.animation=reject==3?1:20;a.frame=1;a.attack=a.dealt=0;a.action=534561;a.command=200;a.state=1025;a.recovery=46;
 a.inputRoutes=reject==1?1:0;d.animation=1;d.kind=12;d.state=226;d.recovery=27;d.pulse=0;
 if(reject==4)d.health=165;
 m.Step(s);
 if(reject==3 || reject==4) { assert(!m.pending && !valid(m));return; }
 for(unsigned i=21;i<=46;++i){a.animation=i;a.frame=i-19;d.animation=i-19;a.recoveryGate=(i==46 && reject!=2);m.Step(s);}
 if(reject) assert(!valid(m));
 else assert(valid(m) && m.outcome==1 && (m.result&65535)==19 && int16_t(m.result>>16)==0);
}
int main(){
 for(unsigned side=0;side<2;++side){guard(side,1);guard(side,2);for(unsigned reject=0;reject<5;++reject)recovery(side,reject);}
 // Unknown recovery types and whiffs must not become a guard result.
 Meter m;Sample s{};s.p[0].attack=1;s.p[0].animation=20;s.p[0].recovery=50;s.p[1].health=180;m.Step(s);
 s.p[0].animation=21;s.p[1].kind=3;s.p[1].state=94;s.p[1].animation=1;s.p[1].recovery=19;s.p[1].pulse=1;m.Step(s);
 s.p[0].animation=22;s.p[1].pulse=0;m.Step(s);assert(!valid(m));
}
