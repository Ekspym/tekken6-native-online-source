#pragma once
#include <cstdint>

namespace t6_frame_panels {
// One atomic snapshot: P1/P2 startup, P1 advantage, advantage-valid bit.
// Startup belongs to the last attack by that player. Advantage is the latest
// contact viewed from each player's perspective, so the two signs are opposite.
inline uint64_t Update(uint64_t previous, uint64_t contact, unsigned attacker) {
  if(attacker>1)return previous;
  const unsigned shift=attacker*16;
  uint64_t next=(previous&0xFFFFFFFFull)&~(0xFFFFull<<shift);
  next|=(contact&0xFFFFull)<<shift;
  if(contact&(uint64_t(1)<<32)) {
    const int advantage=int(int16_t((contact>>16)&0xFFFFu));
    const int p1=attacker==0?advantage:-advantage;
    next|=(uint64_t(uint16_t(int16_t(p1)))<<32)|(uint64_t(1)<<48);
  }
  return next;
}
inline unsigned Startup(uint64_t snapshot,unsigned player) {
  return player<2?unsigned((snapshot>>(player*16))&0xFFFFu):0;
}
inline bool Valid(uint64_t snapshot) {return (snapshot&(uint64_t(1)<<48))!=0;}
inline int Advantage(uint64_t snapshot,unsigned player) {
  const int p1=int(int16_t((snapshot>>32)&0xFFFFu));
  return player==0?p1:-p1;
}
}
