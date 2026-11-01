#include "../../src/practice/t6_transition_policy.h"
#include <cassert>
int main() {
  T6TransitionPolicy p;
  assert(!p.ready(false,100));
  assert(p.ready(true,200));
  assert(p.tick(8199,true));
  assert(!p.tick(8200,true));
  assert(!p.ready(true,8300)); // a held confirm cannot rearm after the timeout
  assert(!p.ready(false,8400));
  assert(p.ready(true,8500));
  assert(!p.tick(8600,false)); // session/rollback/disable restores pacing
  assert(!p.ready(true,8700));
  p.ready(false,8800); assert(p.ready(true,8900));
  p.stop(); assert(!p.active); // battle boundary
  assert(!p.ready(true,9000));
  p.ready(false,9100); assert(p.ready(true,9200));
  assert(!p.ready(false,9201)); // interactive selector restores normal speed
}
