#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../src/practice/t6_after_contact.h"
int main(){
 using namespace t6_after_contact;
 for(unsigned action=0;action<=Backdash;++action)for(unsigned side=0;side<2;++side){
   State state;Sample s{};s.health=180;assert(!state.Step(s,action));
   s.attack=123;assert(!state.Step(s,action)); // a whiff must not arm a response
   s.dealt=10;s.taken=10;s.health=170;s.pulse=1;s.kind=12;s.state=226;s.recovery=20;s.animation=1;
   assert(state.Step(s,action));
   const bool guardChoice=State::GuardChoice(action);
   assert(state.waiting==!guardChoice);
   if(!guardChoice)assert(!state.Buttons(side));
   s.pulse=0;s.animation=10;assert(state.Step(s,action) && state.waiting==!guardChoice);
   s.animation=20;s.readyGate=true;assert(state.Step(s,action) && !state.waiting);
   if(!guardChoice)assert(state.age==0);
   const unsigned back=side?8u:4u,left=side?2u:1u,right=side?1u:2u;
   const unsigned expected[]={0,back,2,back|2u,0,left,right,left,right,back};
   assert(state.Buttons(side)==expected[action]);
   // A snapshot is an exact copy, including contact history and response phase.
   const State saved=state;state.Step(s,action);state=saved;assert(state.age==saved.age && state.Buttons(side)==expected[action]);
   state.Step(s,action);
   if(action>=SidestepLeft)assert(state.Buttons(side)==0);
   state.Step(s,action);
   if(action==Backdash)assert(state.Buttons(side)==back);
   if(action==SidewalkLeft)assert(state.Buttons(side)==left);
   if(action==SidewalkRight)assert(state.Buttons(side)==right);
   for(unsigned i=0;i<30;++i)state.Step(s,action);
   assert(state.active==!(action==SidestepLeft || action==SidestepRight || action==Backdash));
 }
 for(unsigned guard:{1u,2u}){
   State state;Sample s{};s.health=180;state.Step(s,StandingBlock);
   s.attack=1;s.kind=guard;s.state=94;assert(state.Step(s,StandingBlock));
   s.animation=s.recovery=12;s.readyGate=true;state.Step(s,StandingBlock);assert(!state.waiting);
   const auto age=state.age;state.Step(s,StandingBlock);assert(state.age==age+1); // held guard does not create another contact
   s.kind=0;state.Step(s,StandingBlock);s.kind=guard;state.Step(s,StandingBlock);assert(!state.waiting && state.Buttons(0)==4);
 }
 // Guard must already be supplied before any recovery gate is observed,
 // and repeated contacts in a string must never insert a neutral-input gap.
 for(unsigned choice:{unsigned(StandingBlock),unsigned(CrouchingBlock),unsigned(BlockAll)}) {
   State state;Sample s{};s.health=180;state.Step(s,choice);
   s.attack=1;s.pulse=1;s.kind=12;s.state=226;s.dealt=s.taken=10;s.health=170;
   state.Step(s,choice);assert(state.active && !state.waiting);
   const unsigned expected=choice==StandingBlock?4:choice==CrouchingBlock?6:0;
   assert(state.Buttons(0)==expected);
   for(unsigned frame=0;frame<10;++frame){
     s.pulse=frame%2;s.health-=unsigned(s.pulse!=0);state.Step(s,choice);
     assert(!state.waiting && state.Buttons(0)==expected && !s.readyGate);
   }
 }
 State state;Sample s{};state.Step(s,0);s.pulse=1;state.Step(s,0);assert(!state.active); // opponent attacking P1
 state.Reset();assert(!state.initialized && !state.active);
 puts("After-contact: hit/standing+low guard, no whiff/P2 trigger, recovery, all 10 actions, facing and rewind passed.");
}
