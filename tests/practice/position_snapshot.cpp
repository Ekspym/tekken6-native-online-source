#include "../../src/practice/t6_position_snapshot.h"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace t6_position_snapshot;
int main(){
 Chords input;
 assert(input.Poll(true,0,255,0).action==Action::None);
 assert(input.Poll(true,0x20,30,0).action==Action::None);
 auto decision=input.Poll(true,0x20,31,0);assert(decision.consume && decision.action==Action::Save);
 for(int i=0;i<100;++i)assert(input.Poll(true,0x20,255,0).action==Action::None);
 input.Poll(true,0x20,0,0);assert(input.Poll(true,0x20,0,255).action==Action::Load);
 input.Poll(true,0,0,0);decision=input.Poll(true,0x20,255,255);
 assert(decision.consume && decision.action==Action::None);
 assert(input.Poll(true,0x20,0,255).action==Action::None);
 input.Poll(false,0x20,0,0);assert(input.Poll(false,0x20,255,0).action==Action::None);
 assert(input.Poll(true,0x20,255,0).action==Action::None); // no held chord across menu/selection
 input.Poll(true,0,255,0);assert(input.Poll(true,0x20,255,0).action==Action::Save);
 Scope scope;const Key key{1,2,8};
 assert(!scope.Observe(key));assert(!scope.Observe(key));
 assert(scope.Observe({1,3,8}));assert(scope.Observe({1,3,9}));
 scope.Clear();assert(!scope.known);assert(!scope.Observe(key));
 puts("Full-state shortcut edges, thresholds, menu holds and practice scope checks passed.");
}
