// Tekken 6 game hooks - store / address watch, pad step counter, Mokujin style watch, menu save dump (split out of t6_watch.cpp)

#include "t6_watch_internal.h"

extern "C" int t6_watch_on = 0;
extern "C" int t6_rwatch_on = 0;  // T6_RWATCH=1: loads of the T6_WATCH values are logged too

namespace t6w {
std::vector<unsigned long long> g_values;
uint32_t g_from = 0, g_to = 0, g_step = 0;
FILE* g_file = nullptr;
std::mutex g_mutex;
std::set<std::tuple<uint32_t, const char*, unsigned long long>> g_seen;

void Init() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* v = T6Env("T6_WATCH");
  if (!v || !*v) return;
  for (const char* c = v; *c;) {
    char* e = nullptr;
    g_values.push_back(std::strtoull(c, &e, 0));
    if (!e || *e != ',') break;
    c = e + 1;
  }
  if (const char* st = T6Env("T6_WATCH_STEPS")) {
    char* e = nullptr;
    g_from = std::strtoul(st, &e, 0);
    g_to = (e && *e == ',') ? std::strtoul(e + 1, nullptr, 0) : g_from + 600;
  }
  g_file = T6Open(T6DumpPath("watch.txt"), "w");
  if (g_file) setvbuf(g_file, nullptr, _IONBF, 0);
  if (g_file) std::fprintf(g_file, "# watch %zu values, steps %u-%u\n", g_values.size(), g_from, g_to);
}
}  // namespace t6w

// T6_AWATCH=<addr>,<addr>,...: every store to those addresses (any value) in the session frames T6_AWATCH_FRAMES=<a>,<b>
// -> <T6_NET_DUMP_DIR>/awatch.txt (address, value, size, function, frame, thread main/other)
std::vector<uint32_t> g_awatch;
FILE* g_afile = nullptr;
uint32_t T6NetFrameNo();
bool T6NetRunning();
extern "C" PPCContext* t6_frame_ctx;
extern "C" void t6_watch(uint32_t addr, unsigned long long value, int size, const char* fn) {
  if (g_afile) {
    for (const auto a : g_awatch) {
      if (addr <= a && a < addr + static_cast<uint32_t>(size)) {
        std::lock_guard lock(g_mutex);
        const PPCContext* c = t6_thread_bit ? nullptr : t6_frame_ctx;
        std::fprintf(g_afile, "%08X %llX size %d %s frame %u %s lr %08X r3 %08X r4 %08X r11 %08X r25 %08X r26 %08X r29 %08X r30 %08X r31 %08X f0 %g f13 %g\n",
                     addr, value, size, fn, T6NetFrameNo(), t6_thread_bit ? "other" : "main", c ? (uint32_t)c->lr : 0, c ? c->r3.u32 : 0,
                     c ? c->r4.u32 : 0, c ? c->r11.u32 : 0, c ? c->r25.u32 : 0, c ? c->r26.u32 : 0, c ? c->r29.u32 : 0, c ? c->r30.u32 : 0,
                     c ? c->r31.u32 : 0, c ? c->f0.f64 : 0.0, c ? c->f13.f64 : 0.0);
        std::fflush(g_afile);
      }
    }
    if (g_values.empty()) return;
  }
  // any thread
  if (!((addr >= 0x82726000u && addr < 0x82DB0000u) || (addr >= 0xA7290000u && addr < 0xABA90000u))) return;
  if (size == 8) { if ((value >> 32) != 0) return; size = 4; }
  const unsigned long long mask = size == 1 ? 0xFFull : size == 2 ? 0xFFFFull : 0xFFFFFFFFull;
  bool hit = false;
  for (const auto w : g_values) hit |= (value & mask) == w;
  if (!hit) return;
  std::lock_guard lock(g_mutex);
  if (!g_seen.insert({addr, fn, value & mask}).second) return;
  if (g_file) {
    std::fprintf(g_file, "%08X %llu size %d %s step %u\n", addr, value & mask, size, fn, g_step);
    std::fflush(g_file);
  }
}

extern "C" unsigned t6_rwatch(uint32_t addr, unsigned value, int size, const char* fn) {
  if (!((addr >= 0x82726000u && addr < 0x82DB0000u) || (addr >= 0xA7290000u && addr < 0xABA90000u) ||
        (addr >= 0x40000000u && addr < 0x50000000u))) return value;
  bool hit = false;
  for (const auto w : g_values) hit |= value == w;
  if (!hit) return value;
  std::lock_guard lock(g_mutex);
  if (!g_seen.insert({addr | 1u, fn, value}).second) return value;
  if (g_file) std::fprintf(g_file, "R %08X %u size %d %s step %u\n", addr, value, size, fn, g_step);
  return value;
}

void T6CwTick(uint8_t* base);  // (below: T6_CW_ADDR)

// Pad step (once per frame, main thread): counts the steps and switches the watch on in the window
extern "C" void __imp__sub_82268A60(PPCContext& ctx, uint8_t* base);
extern "C" double t6_pad_step_ms;
extern "C" void sub_82268A60(PPCContext& ctx, uint8_t* base) {
  Init();
  g_step++;
  if (g_file && g_step % 500 == 0) std::fprintf(g_file, "# step %u\n", g_step);
  t6_watch_on = (g_file && g_step >= g_from && g_step <= g_to) ? 1 : 0;
  static const char* aw = T6Env("T6_AWATCH");
  if (aw && *aw) {
    static uint32_t af = 0, at = 0;
    if (!g_afile) {
      for (const char* c = aw; *c;) {
        char* e = nullptr;
        g_awatch.push_back(static_cast<uint32_t>(std::strtoul(c, &e, 0)));
        if (!e || *e != ',') break;
        c = e + 1;
      }
      const char* fr = T6Env("T6_AWATCH_FRAMES");
      af = fr ? std::strtoul(fr, nullptr, 0) : 0;
      at = fr && std::strchr(fr, ',') ? std::strtoul(std::strchr(fr, ',') + 1, nullptr, 0) : af + 10;
      const char* d = T6Env("T6_NET_DUMP_DIR");
      g_afile = T6Open((std::string(d ? d : ".") + "/awatch.txt"), "w");
    }
    if (T6NetRunning() && T6NetFrameNo() + 1 >= af && T6NetFrameNo() <= at) t6_watch_on = 1;
  }
  T6CwTick(base);
  static const bool rw = T6Env("T6_RWATCH") != nullptr;
  t6_rwatch_on = rw ? t6_watch_on : 0;
  t6_game_info_step(base, g_step);
  LARGE_INTEGER f, a, b;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&a);
  __imp__sub_82268A60(ctx, base);
  QueryPerformanceCounter(&b);
  t6_pad_step_ms += (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
}


// T6_CW_ADDR=<guest addr>, T6_CW_FRAMES=<a>,<b> (research 2026-10-16, Mokujin's style +0x54 of the fight player): the
// accesses to those 4 bytes in the session frames a..b, by guest function (the custom watch above); every pad step
// T6_CW_STEPS=<a>,<b> instead: pad steps a..b (also before the session); T6_CW_LEN = bytes watched (default 4)
void T6CwTick(uint8_t* base) {
  static const char* cw = T6Env("T6_CW_ADDR");
  static const char* st = T6Env("T6_CW_STEPS");
  if (!cw || !*cw || (!st && !T6NetRunning())) return;
  static const char* fr = st ? st : T6Env("T6_CW_FRAMES");
  static const uint32_t ca = fr ? std::strtoul(fr, nullptr, 0) : 0;
  static const uint32_t cb = fr && std::strchr(fr, ',') ? std::strtoul(std::strchr(fr, ',') + 1, nullptr, 0) : ca + 10;
  static const uint32_t len = T6EnvU32("T6_CW_LEN", 4, 0);
  static bool done = false;
  const uint32_t f = st ? g_step : T6NetFrameNo();
  if (!done && f >= ca && f <= cb) CwStart(base, std::strtoul(cw, nullptr, 0), len);
  if (!done && f > cb && g_cw.lo) {
    CwStop();
    done = true;
  }
}

// T6_WATCH_SAVE=<menu seq step> (research 2026-10-15, the item shop's unlock / ownership data): at that step of
// T6_MENU_SEQ the in-memory image of MAINDATA.SAV (+4 length 0x0038DA08, +8 "SYS") is searched in the heap and
// watched as a whole (reads / writes logged per function and 16-byte block, offsets = file offsets)
void T6WatchSave(uint8_t* base) {
  MEMORY_BASIC_INFORMATION mi;
  for (uint8_t* a = base + 0x40000000ull; a < base + 0x100000000ull; a += mi.RegionSize) {
    if (!VirtualQuery(a, &mi, sizeof(mi)) || !mi.RegionSize) break;
    if (mi.State != MEM_COMMIT || !(mi.Protect & PAGE_READWRITE) || (mi.Protect & PAGE_GUARD)) continue;
    uint8_t* end = static_cast<uint8_t*>(mi.BaseAddress) + mi.RegionSize;
    // the file's bytes 0x10..0x30 (the start of the SYS block after its header)
    static const uint8_t pat[32] = {1, 1, 2, 0, 0, 0, 0, 0x0A, 0x0A, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0};
    for (uint8_t* q = static_cast<uint8_t*>(mi.BaseAddress); q + 32 <= end; q += 4)
      if (std::memcmp(q, pat, 32) == 0) {
        const uint32_t g = static_cast<uint32_t>(q - base) - 0x10;  // = file offset 0
        REXLOG_INFO("T6 watch save: image at {:08X} (header {:08X} {:08X})", g, __builtin_bswap32(*reinterpret_cast<uint32_t*>(q - 16)),
                    __builtin_bswap32(*reinterpret_cast<uint32_t*>(q - 12)));
        // T6_SAVE_POKE=<file offset>=<u32>,... (research): written into the image first
        for (const char* pk = T6Env("T6_SAVE_POKE"); pk && *pk;) {
          char* e = nullptr;
          const uint32_t off = std::strtoul(pk, &e, 10);
          if (*e != '=') break;
          const uint32_t v = std::strtoul(e + 1, &e, 10);
          *reinterpret_cast<uint32_t*>(base + g + off) = __builtin_bswap32(v);
          REXLOG_INFO("T6 watch save: +{} = {}", off, v);
          if (*e != ',') break;
          pk = e + 1;
        }
        if (!T6Env("T6_SAVE_NOWATCH")) CwStart(base, g, 0x0038DA08u);
        return;
      }
  }
  REXLOG_INFO("T6 watch save: no image found");
}
