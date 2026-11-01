#pragma once
#include <cmath>
#include <cstdint>

namespace t6_position_snapshot {
enum class Action : unsigned { None, Save, Load };
enum class Status : unsigned { Empty, Saved, Loaded, NoSave, Changed, Invalid, Failed };
struct Decision { Action action=Action::None; bool consume=false; };
struct Chords {
  unsigned previous=0;
  Decision Poll(bool enabled,uint16_t buttons,uint8_t lt,uint8_t rt) {
    const unsigned held=(buttons&0x20)?unsigned(lt>30)|(unsigned(rt>30)<<1):0;
    const unsigned edge=held&~previous;
    previous=held;
    if(!enabled || !held)return {};
    // Simultaneous triggers consume the shortcut without choosing an action.
    // Releasing just one cannot turn an ambiguous chord into another action.
    return {held==1 && (edge&1)?Action::Save:
            held==2 && (edge&2)?Action::Load:Action::None,true};
  }
};
struct Key {
  uint32_t player=0,opponent=0,stage=0;
  bool operator==(const Key& b) const {return player==b.player && opponent==b.opponent && stage==b.stage;}
};
struct Scope {
  bool known=false;Key current{};
  bool Observe(Key key) {
    const bool changed=known && !(key==current);
    current=key;known=true;return changed;
  }
  void Clear(){*this=Scope{};}
};
}
