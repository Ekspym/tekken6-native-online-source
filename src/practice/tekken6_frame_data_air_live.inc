#pragma once
#include "t6_frame_data_panels.h"
#include "tekken6_frame_data_air_measure.inc"
#include "tekken6_frame_data_low_probe.inc"
#include "tekken6_frame_data_air_rules.inc"
namespace tekken_contacts_live {
std::atomic<uint64_t> result{0},panels{0};std::mutex mutex;
tekken_contacts_measure::Meter meter;uint32_t owner=0;
}
bool TekkenFrameDataPracticeActive(){return practiceRageOwner.load(std::memory_order_acquire)!=0;}
uint64_t TekkenFrameDataPanels(){return tekken_contacts_live::panels.load(std::memory_order_acquire);}
uint64_t TekkenFrameDataResult(){return tekken_contacts_live::result.load(std::memory_order_acquire);}
void CaptureTekkenFrameDataCandidates(uint32_t held) {
 using namespace tekken_contacts_live;
 std::lock_guard<std::mutex> lock(mutex);
 const uint32_t currentOwner=practiceRageOwner.load(std::memory_order_acquire);
 if(owner!=currentOwner){owner=currentOwner;meter.Reset();result.store(0,std::memory_order_release);panels.store(0,std::memory_order_release);}
 if(!currentOwner)return;
 auto* runtime=rex::Runtime::instance();auto* b=runtime?runtime->virtual_membase():nullptr;if(!b)return;
 auto word=[&](uint32_t a){return uint32_t(b[a])<<24|uint32_t(b[a+1])<<16|uint32_t(b[a+2])<<8|b[a+3];};
 tekken_contacts_measure::Sample sample{};
 for(unsigned i=0;i<2;++i){const uint32_t a=i?0x82901840u:0x82900C00u;
   sample.p[i]={word(a+0xDC),word(a+0xC4),word(a+0x1AC),word(a+0x1D0),word(a+0x1D4),word(a+0x214),
     word(a+0x1C4),word(a+0x1E8),word(a+0x60),word(a+0x3CC),word(a+0x10C),word(a+0x108),word(a+0x1A8),0};
   sample.p[i].recoveryGate=b[a+916] && b[a+917];
   sample.p[i].inputRoutes=tekken_common_rules::HasInputs(word,sample.p[i].command,sample.p[i].animation,word(a+0x84));
   if(sample.p[i].state&0x400u || sample.p[i].attack || (meter.pending && meter.attacker==i))
     sample.p[i].stanceBoundary=tekken_common_rules::Boundary(word,sample.p[i].command,sample.p[i].animation,int32_t(word(a+0x19C)),word(a+0x84));
 }
 const bool changed=meter.Step(sample);
 tekken_low_contact_probe::Capture(currentOwner,held,sample,meter);
 if(changed) {
   result.store(meter.result,std::memory_order_release);
   panels.store(t6_frame_panels::Update(panels.load(std::memory_order_relaxed),meter.result,meter.resultAttacker),std::memory_order_release);
   static unsigned logged=0;
   if(meter.result && logged++<300)if(auto* f=std::fopen("practice-frame-air-result.txt","a")) {
     std::fprintf(f,"event=%u class=%s startup=%u",logged,meter.outcome==2?"guard":meter.outcome==1?"hit":meter.outcome==3?"unsupported":"pending",unsigned(meter.result&0xFFFFu));
     if(meter.result&(uint64_t(1)<<32))std::fprintf(f," advantage=%+d",int(int16_t((meter.result>>16)&0xFFFFu)));
     else std::fputs(" advantage=--",f);
     if(meter.stance)std::fprintf(f," stance-boundary=%u defender-ready=%u",sample.p[meter.attacker].stanceBoundary,meter.readyFrame);
     std::fputc('\n',f);std::fclose(f);
   }
 }
}
