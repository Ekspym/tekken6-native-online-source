// Native practice menu and recording are owned by t6_advanced_practice.cpp.
#include "t6_netplay_internal.h"
#include "t6_advanced_practice.h"
std::mutex g_t6_in_mutex;
std::deque<T6InputRow> g_t6_in_rows;
std::atomic<bool> g_t6_in_display{true};
std::atomic<bool> g_t6_fa_display[2]{true,true};
void PracticeTick() {}
void PracticePadUser0(uint8_t*,bool) {}
void PracticeRawPad(uint16_t) {}
bool PracticeDummyStart() { return false; }
bool PracticeDummyPad() { return false; }
bool PracticeTakeRebattle() { return false; }
void PracticeNewBattle() { std::lock_guard lock(g_t6_in_mutex); g_t6_in_rows.clear(); }
static void NoteInputRow(const uint8_t* p1) {
  const uint32_t w = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p1 + 0xC));
  T6InputRow r;
  r.dirs = static_cast<uint8_t>((w >> 20) & 0xF);
  r.buttons = static_cast<uint8_t>(((w & 0x04000000u) ? 1 : 0) | ((w & 0x01000000u) ? 2 : 0) | ((w & 0x00040000u) ? 4 : 0) |
                                   ((w & 0x00020000u) ? 8 : 0));
  std::lock_guard lock(g_t6_in_mutex);
  if (!g_t6_in_rows.empty() && g_t6_in_rows.front().dirs == r.dirs && g_t6_in_rows.front().buttons == r.buttons) {
    if (g_t6_in_rows.front().frames < 999) g_t6_in_rows.front().frames++;
    return;
  }
  g_t6_in_rows.push_front(r);
  if (g_t6_in_rows.size() > 18) g_t6_in_rows.pop_back();
}

void PracticeEntries(uint8_t* p1,uint8_t*) {
  if (T6AdvancedGuestAvailable() && g_practice_battle) NoteInputRow(p1);
}
