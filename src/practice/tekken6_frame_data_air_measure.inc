#pragma once
namespace tekken_contacts_measure {
// LOW CONTACT SUPPORT V1: native standing and crouching guard reactions.
inline bool GuardKind(uint32_t kind) { return kind==1 || kind==2; }
struct Fighter {uint32_t frame,animation,attack,dealt,taken,kind,state,recovery,health,pulse,action,command,sequence,stanceBoundary;uint32_t recoveryGate=0,inputRoutes=0;};
struct Sample {Fighter p[2];};
struct Meter {
 Sample previous{};bool initialized=false,pending=false;
 unsigned attacker=0,startup=0,pendingType=0;uint32_t health=0,attackRecovery=0,action=0;
 bool recoveryPhase=false,blockRecoil=false;uint32_t contactDamage=0;
 bool stance=false,defenderReady=false,possibleInput=false;uint32_t readyFrame=0,lastAnimation=0;
 uint64_t result=0;unsigned resultAttacker=0,outcome=0; // 1 hit, 2 guard, 3 unsupported recovery
 void Reset(){*this=Meter{};}
 bool Step(const Sample& s) {
   if(!initialized){previous=s;initialized=true;return false;}
   const uint64_t before=result;bool event=false;
   for(unsigned i=0;i<2;++i) {
     const auto& a=s.p[i];const auto& d=s.p[1-i];const auto& pa=previous.p[i];const auto& pd=previous.p[1-i];
     if(!a.attack)continue;
     const bool pulse=d.pulse && d.pulse!=pd.pulse;
     const bool hit=a.dealt && d.taken && (a.dealt!=pa.dealt || d.taken!=pd.taken || d.health<pd.health);
     const bool waitingGuard=pending && attacker==i && pendingType==2 && !defenderReady &&
       a.animation>=startup && a.animation<=startup+6 && d.health==health;
     const bool guard=GuardKind(d.kind) && !GuardKind(pd.kind) && !a.dealt && !d.taken && !waitingGuard;
     if(!(pulse || hit || guard))continue;
     pending=false;outcome=0;result=0;resultAttacker=i;event=true;
     if(!a.animation || a.animation>120)continue;
     startup=a.animation;result=startup;
     stance=(a.state&0x400u)!=0 || a.stanceBoundary!=0;possibleInput=a.inputRoutes!=0;defenderReady=false;readyFrame=0;lastAnimation=a.animation;
     pendingType=(d.kind==12 || d.kind==0) && a.dealt && d.taken?1:
       (GuardKind(d.kind) || d.kind==0) && !a.dealt && !d.taken && d.health==pd.health?2:0;
     if(!pendingType){outcome=3;continue;}
     recoveryPhase=false;blockRecoil=false;contactDamage=d.taken;
     attacker=i;health=d.health;attackRecovery=a.recovery;action=a.action;pending=true;
   }
   if(pending) {
     const auto& a=s.p[attacker];const auto& d=s.p[1-attacker];
     // BLOCK RECOIL SUPPORT V1: a blocked move can force the attacker into
     // a fresh nonattacking script. Pair the two newly settled reaction clocks
     // instead of comparing the recoil's C4 with the original attack startup.
     // Wait for the native gate at the recoil endpoint before publishing.
     const bool recoil=!blockRecoil && pendingType==2 && !d.pulse &&
       !a.attack && !a.dealt && !d.taken && d.health==health &&
       a.animation>0 && a.animation<=2 && a.frame>0 && a.frame<=2 &&
       a.animation<startup && previous.p[attacker].animation>=startup &&
       a.command!=previous.p[attacker].command && !a.inputRoutes &&
       GuardKind(d.kind) && d.state==94 && d.animation>0 && d.animation<=2 &&
       a.recovery>=a.animation && a.recovery<=600 &&
       d.recovery>=d.animation && d.recovery<=600;
     if(recoil) {
       blockRecoil=true;stance=true;recoveryPhase=true;possibleInput=false;
       action=a.action;attackRecovery=a.recovery;lastAnimation=a.animation;
       readyFrame=a.animation+d.recovery-d.animation;defenderReady=true;
     }
     // Some lows change from the hit script into an automatic recovery script
     // on the next sample. Blocked rising attacks can keep a new attack ID in
     // that recovery phase. DC resets but C4 stays continuous. Do not confuse
     // this with a queued string attack, whose C4 resets as well.
     const bool nextRecoveryPhase=!blockRecoil && a.animation>startup && a.animation<=startup+6 &&
       a.animation>=lastAnimation && a.frame>0 && a.frame<=2 &&
       (!a.attack || pendingType==2) &&
       (a.state&0x400u) && (a.action!=action || a.command!=previous.p[attacker].command) &&
       d.health==health && ((pendingType==1 && contactDamage && d.taken==contactDamage) ||
                           (pendingType==2 && !a.dealt && !d.taken));
     if(nextRecoveryPhase) {
       stance=true;recoveryPhase=true;attackRecovery=a.recovery;action=a.action;
     }
     const bool damageMatches=pendingType==2?(!a.dealt && !d.taken):
       ((a.dealt && d.taken) || (recoveryPhase && contactDamage && d.taken==contactDamage));
     if(stance) {
       possibleInput=possibleInput || a.inputRoutes!=0;
       // Keep the contact across internal script phases. C4 is continuous here;
       // segment DC, attack flags and nominal recovery can change independently.
       if(a.action!=action || a.animation<lastAnimation || a.animation>(blockRecoil?600u:startup+120) || d.health!=health) {
         pending=false;outcome=3;
       } else {
         if(!defenderReady && a.animation>startup && a.animation<=startup+6 &&
            damageMatches && !d.pulse && d.animation>0 && d.animation<=2 &&
            d.recovery>=d.animation && d.recovery<=600 &&
            ((pendingType==2 && GuardKind(d.kind) && d.state==94) ||
             (pendingType==1 && d.kind==12 && d.state==226))) {
           readyFrame=a.animation+d.recovery-d.animation;defenderReady=true;
         }
         // An airborne state alone is not a stance. Only use the reported
         // recovery after the native scheduler agrees at that endpoint, with
         // no basic-input routes observed across the pending script phases.
         if(defenderReady && !possibleInput && a.recoveryGate &&
            a.recovery>0 && a.recovery==attackRecovery && a.animation>=a.recovery && a.animation<=a.recovery+2 && a.recovery<=600) {
           const int advantage=int(readyFrame)-int(a.recovery);
           if(advantage>=-120 && advantage<=120) {
             result=uint64_t(startup)|(uint64_t(uint16_t(int16_t(advantage)))<<16)|(uint64_t(1)<<32);
             outcome=pendingType;event=true;
           }
           pending=false;
         } else if(!blockRecoil && defenderReady && a.stanceBoundary>=a.animation && a.stanceBoundary<=600) {
           const int advantage=int(readyFrame)-int(a.stanceBoundary);
           if(advantage>=-120 && advantage<=120) {
             result=uint64_t(startup)|(uint64_t(uint16_t(int16_t(advantage)))<<16)|(uint64_t(1)<<32);
             outcome=pendingType;event=true;
           }
           pending=false;
         }
         if(!defenderReady && a.animation>startup+6){pending=false;outcome=3;}
       }
       lastAnimation=a.animation;
     } else if(!a.attack || !damageMatches || d.health!=health || a.animation<startup ||
        a.animation>startup+6 || a.recovery!=attackRecovery || a.action!=action || (a.state&0x400u)) {
       pending=false;outcome=3;
     } else if(a.animation>startup && !d.pulse && d.animation>0 && d.animation<=2 &&
               ((pendingType==2 && GuardKind(d.kind) && d.state==94) ||
                (pendingType==1 && d.kind==12 && d.state==226))) {
       // The contact notification may arrive while the preceding reaction is
       // still present. Wait for its animation counter to reset before pairing.
       if(a.recovery>=a.animation && d.recovery>=d.animation && a.recovery<=600 && d.recovery<=600) {
         const int advantage=int(d.recovery-d.animation)-int(a.recovery-a.animation);
         if(advantage>=-120 && advantage<=120) {
           result=uint64_t(startup)|(uint64_t(uint16_t(int16_t(advantage)))<<16)|(uint64_t(1)<<32);
           outcome=pendingType;event=true;
         }
       }
       pending=false;
     }
   }
   previous=s;return event || result!=before;
 }
};
}
