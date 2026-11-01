// Bounded loading acceleration; deliberately independent of guest clock speed.
#pragma once
#include <cstdint>
struct T6TransitionPolicy {
  bool active=false;
  uint64_t deadline=0;
  bool confirmHeld=false;
  bool ready(bool confirmed,uint64_t now) {
    if (!confirmed) { active=false; confirmHeld=false; return false; }
    if (!confirmHeld) { active=true; deadline=now+8000; }
    confirmHeld=true;
    return active;
  }
  bool tick(uint64_t now,bool allowed) {
    if (!allowed || (active && now>=deadline)) active=false;
    return active;
  }
  void stop() { active=false; }
};
