#pragma once
#include <cstdint>
namespace t6_after_contact {
enum Choice:unsigned {Standing,StandingBlock,Crouching,CrouchingBlock,BlockAll,
  SidestepLeft,SidestepRight,SidewalkLeft,SidewalkRight,Backdash};
struct Sample {uint32_t attack=0,dealt=0,taken=0,health=0,pulse=0,kind=0,animation=0,recovery=0;bool readyGate=false;uint32_t state=0;};
struct State {
 Sample previous{};bool initialized=false,active=false,waiting=false;
 unsigned choice=0,age=0,wait=0;
 void Reset(){*this=State{};}
 static bool Guard(uint32_t kind){return kind==1 || kind==2;}
 static bool GuardChoice(unsigned selected){return selected==StandingBlock || selected==CrouchingBlock || selected==BlockAll;}
 bool Step(const Sample& s,unsigned selected){
   if(selected>Backdash){Reset();return false;}
   const bool contact=initialized && s.attack &&
     ((s.pulse && s.pulse!=previous.pulse) ||
      (s.dealt && s.taken && (s.dealt!=previous.dealt || s.taken!=previous.taken || s.health<previous.health)) ||
      (Guard(s.kind) && !Guard(previous.kind) && !s.dealt && !s.taken));
   previous=s;initialized=true;
   if(active && choice!=selected){active=false;waiting=false;}
   if(contact){
     active=true;waiting=!GuardChoice(selected);choice=selected;age=0;wait=0;
     // Hold guard during the reaction: native hit/blockstun still decides when
     // it can take effect. Waiting for a neutral/recovery sample loses gaps.
     if(!waiting)return true;
   }
   if(!active)return false;
   if(waiting){
     ++wait;
     const bool recoveryEnd=s.readyGate && (s.state==94 || s.state==226) &&
       s.recovery && s.recovery<=600 && s.animation>=s.recovery;
     const bool neutral=s.kind==0 && !s.pulse;
     if(wait>=2 && !s.pulse && (neutral || recoveryEnd)){waiting=false;age=0;}
     return true;
   }
   if(age<600)++age;
   if((choice==SidestepLeft || choice==SidestepRight || choice==Backdash) && age>=24)active=false;
   return active;
 }
 uint32_t Buttons(unsigned facing) const {
   if(!active || waiting || facing>1)return 0;
   const uint32_t back=facing?8u:4u;
   const uint32_t left=facing?2u:1u,right=facing?1u:2u;
   switch(choice){
     case StandingBlock:return back;
     case Crouching:return 2;
     case CrouchingBlock:return back|2u;
     case SidestepLeft:return age==0?left:0;
     case SidestepRight:return age==0?right:0;
     case SidewalkLeft:return age==1?0:left;
     case SidewalkRight:return age==1?0:right;
     case Backdash:return age==0 || age==2?back:0;
     default:return 0;
   }
 }
};
struct Runtime {
 State action;
 uint32_t owner=0,fighter=0,mode=0,input=0,control=0,written=0;
 bool owned=false;
};
}
