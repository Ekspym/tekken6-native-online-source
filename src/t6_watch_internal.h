// Tekken 6 game hooks - declarations shared by the modules split out of t6_watch.cpp
#pragma once

#include "t6_common.h"
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>



// ---- shared between the modules (global scope)
extern "C" void t6_game_info_step(uint8_t* base, uint32_t step);  // t6_match_setup.cpp
extern std::set<uint32_t> g_t6_profiles;  // t6_research.cpp
extern std::mutex g_t6_profiles_mutex;  // t6_research.cpp

namespace t6w {

// T6_CUSTOM_WATCH / T6_CW_ADDR / T6_WATCH_SAVE (t6_research.cpp): guest pages watched through access violations
struct CustWatch {
  uint8_t* lo = nullptr;  // watched host pages
  uint8_t* hi = nullptr;
  uint8_t* rec = nullptr;  // the record (80 bytes)
  uint32_t len = 80;       // (T6_WATCH_SAVE: the whole save image)
  uint32_t guest = 0;
  uint32_t logged = 0;
  std::vector<std::pair<uintptr_t, uint32_t>> funcs;  // host address -> guest address, sorted
  std::mutex m;
  std::set<std::pair<uint32_t, uint32_t>> seen;  // (guest function, offset)
};
extern CustWatch g_cw;  // t6_research.cpp
void CwStart(uint8_t* base, uint32_t rec, uint32_t len = 80);  // t6_research.cpp
void CwStop();  // t6_research.cpp
extern uint32_t g_step;  // t6_watch.cpp: pad steps

}  // namespace t6w
using namespace t6w;

