#pragma once
namespace tekken_common_rules {
inline bool Valid(uint32_t a,unsigned size){return a>=0x90000000u && a<=0xC0000000u-size && !(a&3u);}
template<class Word>
bool HasInputs(Word word,uint32_t command,uint32_t animation,uint32_t root){
 // Missing/ambiguous metadata prohibits the airborne recovery fallback.
 if(!Valid(command,88))return true;
 uint32_t p=word(command+20),resume=0;
 for(unsigned n=0;n<256;++n){
   if(!Valid(p,32))return true;
   const uint32_t kind=word(p);
   if(kind==0x8000u)return false;
   if(kind==0x800Cu){if(!resume)return true;p=resume;resume=0;continue;}
   if(kind==0x800Bu){
     if(resume || !Valid(root,424))return true;
     const uint32_t pool=word(root+420),i=word(p+28)>>16;
     if(!Valid(pool,32) || i>(0xC0000000u-pool-32u)/32u)return true;
     resume=p+32;p=pool+i*32;continue;
   }
   if(kind>=0x8000u)return true;
   const uint32_t input=word(p+4);
   if((input&0x40000000u) && (input&15u) && word(p+20)>=animation)return true;
   p+=32;
 }
 return true;
}
template<class Word> bool Always(Word word,uint32_t a){
 // sub_8229D3D8 opcode0 returns true; sub_82285FC0 ends the list at397.
 // Unknown predicates are NOT evaluated as true.
 for(unsigned i=0;i<16;++i){
   if(!Valid(a,8))return false;
   const auto op=word(a);if(op==397)return true;if(op!=0)return false;a+=8;
 }
 return false;
}
template<class Word>
uint32_t Boundary(Word word,uint32_t command,uint32_t animation,int32_t offset,uint32_t root){
 if(!Valid(command,88) || offset<0)return 0;
 const uint32_t duration=word(command+36);if(!duration || duration>600)return 0;
 uint32_t p=word(command+20),resume=0;
 uint32_t first[4]={},begin[4]={},end[4]={},effect[4]={};bool terminated=false;
 for(unsigned budget=0;budget<256;++budget){
   if(!Valid(p,32))return 0;
   const uint32_t kind=word(p),input=word(p+4),target=word(p+28);
   if(kind==0x8000u){terminated=true;break;}
   if(kind==0x800Cu){if(!resume)return 0;p=resume;resume=0;continue;}
   if(kind==0x800Bu){
     if(resume || !Valid(root,424))return 0;
     const uint32_t pool=word(root+420),index=target>>16;
     if(!Valid(pool,32) || index>(0xC0000000u-pool-32u)/32u)return 0;
     resume=p+32;p=pool+index*32;continue;
   }
   if(kind>=0x8000u)return 0;
   const uint32_t buttons=input&15u;
   if(!kind && (input&0xFFFFFFF0u)==0x40000000u && buttons && !(buttons&(buttons-1u)) &&
      (target&65535u)==0x50u && (target>>16) && Always(word,word(p+8))) {
     uint32_t execute=word(p+24);if(execute==0x7FFFu)execute=duration;
     const uint32_t low=word(p+16),high=word(p+20),effects=word(p+12);
     if(!Valid(effects,4) || word(effects)==16383u)return 0;
     if(!execute || low>execute || execute>high || execute>duration)return 0;
     unsigned i=buttons==1?0:buttons==2?1:buttons==4?2:3;
     if(!first[i] || execute<first[i]){first[i]=execute;begin[i]=low;end[i]=high;effect[i]=effects;}
     else if(execute==first[i]){if(low<begin[i])begin[i]=low;if(high>end[i])end[i]=high;}
   }
   p+=32;
 }
 if(!terminated)return 0;
 uint32_t earliest=0;
 for(unsigned i=0;i<4;++i)if(first[i] && (!earliest || first[i]<earliest))earliest=first[i];
 if(!earliest || earliest<animation)return 0;
 // Stances can unlock basic attacks at different times. Use the earliest
 // ordinary boundary only when multiple independent buttons corroborate it.
 // A lone earlier cancel is ambiguous; do not skip it for a later pair.
 unsigned all=0;
 for(unsigned i=0;i<4;++i)if(first[i]==earliest && animation>=begin[i] && animation<=end[i])++all;
 if(all==4)return earliest;
 // Two string follow-ups can share a cancel frame but enter different poses.
 // For a partial set, require identical windows and transition effects too.
 for(unsigned i=0;i<4;++i){
   if(first[i]!=earliest || animation<begin[i] || animation>end[i])continue;
   unsigned count=0;
   for(unsigned j=0;j<4;++j)if(first[j]==earliest && begin[j]==begin[i] && end[j]==end[i] && effect[j]==effect[i])++count;
   if(count>=2)return earliest;
 }
 return 0;
}
}
