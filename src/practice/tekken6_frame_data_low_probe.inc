// Opt-in read-only samples for low-hit/low-guard classification.
#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
namespace tekken_low_contact_probe {
struct Row {
  long long us;uint32_t owner,held;
  tekken_contacts_measure::Sample sample;
  bool pending,stance,ready;unsigned attacker,type,outcome;
  uint64_t result;
};
inline void Capture(uint32_t owner,uint32_t held,const tekken_contacts_measure::Sample& sample,
                    const tekken_contacts_measure::Meter& meter) {
  static const bool enabled=[] {
    const char* value=T6PracticeEnv("TEKKEN6_TRACE_LOW_CONTACTS");
    return value && std::strcmp(value,"1")==0;
  }();
  if(!enabled)return;
  // Called under tekken_contacts_live::mutex. Never writes guest state or inputs.
  static std::array<Row,30> preroll{};
  static unsigned head=0,available=0,count=0;
  static uint32_t captureOwner=0;
  static std::FILE* file=nullptr;
  static bool started=false,done=false;
  if(done)return;
  if(captureOwner!=owner) {
    if(started){if(file){std::fflush(file);std::fclose(file);file=nullptr;}done=true;return;}
    captureOwner=owner;head=available=0;
  }
  Row row{std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count(),owner,held,sample,
      meter.pending,meter.stance,meter.defenderReady,meter.attacker,meter.pendingType,meter.outcome,meter.result};
  auto write=[&](const Row& r) {
    std::fprintf(file,"%u,%lld,%u,%u",++count,r.us,r.owner,r.held);
    for(const auto& p:r.sample.p)std::fprintf(file,
      ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
      p.frame,p.animation,p.attack,p.dealt,p.taken,p.kind,p.state,p.recovery,p.health,p.pulse,
      p.action,p.command,p.sequence,p.stanceBoundary,p.recoveryGate,p.inputRoutes);
    std::fprintf(file,",%u,%u,%u,%u,%u,%u,%llu\n",unsigned(r.pending),unsigned(r.stance),
      unsigned(r.ready),r.attacker,r.type,r.outcome,static_cast<unsigned long long>(r.result));
  };
  if(!started) {
    if(!sample.p[0].attack && !sample.p[1].attack) {
      preroll[head]=row;head=(head+1)%30;if(available<30)++available;return;
    }
    file=std::fopen("practice-low-contacts.csv","w");
    if(!file){done=true;return;}
    std::fputs("sample,host_us,owner,held",file);
    for(unsigned side=1;side<=2;++side)
      for(const char* field:{"frame","animation","attack","dealt","taken","kind","state","recovery",
                             "health","pulse","action","command","sequence","stanceBoundary","recoveryGate","inputRoutes"})
        std::fprintf(file,",p%u_%s",side,field);
    std::fputs(",pending,stance,defenderReady,attacker,pendingType,outcome,result\n",file);
    started=true;
    for(unsigned i=0;i<available;++i)write(preroll[(head+30-available+i)%30]);
  }
  write(row);
  if(count%30==0)std::fflush(file);
  if(count>=9000){std::fflush(file);std::fclose(file);file=nullptr;done=true;}
}
}
