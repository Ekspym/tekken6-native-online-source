// Tekken 6 netplay - research dumps (moves, fight players) (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// T6_MOVE_DUMP=<file> (research, 2026-10-14: when can a player act after a move - the game's cancel data): when a
// player's move id (+0x1C0) changes, the 0x100 bytes at +0x104 (a heap pointer that changes with every move) and
// 0x60 bytes at each pointer found in them, as hex words.
static void MoveStructDump() {
  // T6_GROUP_PROBE=1 (research): once in the fight, the player object's words +0x80..+0x110 and for each pointer P
  // among them the word at P + 0x1A4 (the AI code's cancel group table: moveset + 420) and the first group entry
  if (static bool once = false; !once && T6Env("T6_GROUP_PROBE")) {
    const uint32_t pl = T6PlayerPtr(0);
    if (pl >= 0x82726000u && pl < 0x82DB0000u && T6Rd32(pl + 0x1C0) == 0x8001u) {
      once = true;
      for (uint32_t o = 0x80; o <= 0x110; o += 4) {
        const uint32_t q = T6Rd32(pl + o);
        if ((q >= 0xA7290000u && q + 0x200 < 0xABA90000u) || (q >= 0x82726000u && q + 0x200 < 0x82DB0000u)) {
          const uint32_t g = T6Rd32(q + 0x1A4);
          REXLOG_INFO("T6 group probe: +{:X} = {:08X} -> +1A4 {:08X}{}", o, q, g,
                      (g >= 0xA7290000u && g < 0xABA90000u) ? fmt::format(" group 1309: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                                                                             T6Rd32(g + 1309 * 32), T6Rd32(g + 1309 * 32 + 4), T6Rd32(g + 1309 * 32 + 8), T6Rd32(g + 1309 * 32 + 12),
                                                                             T6Rd32(g + 1309 * 32 + 16), T6Rd32(g + 1309 * 32 + 20), T6Rd32(g + 1309 * 32 + 24), T6Rd32(g + 1309 * 32 + 28))
                                                                 : std::string());
        }
      }
    }
  }
  static const char* path = T6Env("T6_MOVE_DUMP");
  if (!path || !*path) return;
  static FILE* f = T6Open(path, "w");
  if (!f) return;
  static uint32_t prev_id[2] = {}, frame = 0;
  frame++;
  const auto words = [&](uint32_t a, uint32_t n) {
    for (uint32_t o = 0; o < n; o += 4) std::fprintf(f, "%s%08X", o % 32 ? " " : (o ? "\n    " : "    "), T6Rd32(a + o));
    std::fprintf(f, "\n");
  };
  for (int n = 0; n < 2; n++) {
    const uint32_t pl = T6PlayerPtr(n);
    if (!T6InGameRange(pl, 0xC40)) continue;
    const uint32_t id = T6Rd32(pl + 0x1C0), p = T6Rd32(pl + 0x104);
    if (id == prev_id[n]) continue;
    prev_id[n] = id;
    std::fprintf(f, "%u P%d move %u ptr %08X +1C4 %X\n", frame, n + 1, id, p, T6Rd32(pl + 0x1C4));
    if (!T6InGameRange(p, 0x100)) continue;
    // move struct 0x58 bytes: +0x14 cancel list (0x20 per entry: command 64 bit, requirements, extra, window start,
    // window end, frame, move id << 16 | option), +0x4C / +0x50 first / last active frame
    std::fprintf(f, "  struct: %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X %08X %08X %08X\n", T6Rd32(p), T6Rd32(p + 4), T6Rd32(p + 8),
                 T6Rd32(p + 0xC), T6Rd32(p + 0x10), T6Rd32(p + 0x14), T6Rd32(p + 0x18), T6Rd32(p + 0x1C), T6Rd32(p + 0x24), T6Rd32(p + 0x40), T6Rd32(p + 0x44), T6Rd32(p + 0x48),
                 T6Rd32(p + 0x4C), T6Rd32(p + 0x50));
    for (uint32_t c = T6Rd32(p + 0x14), k = 0; T6InGameRange(c, 0x20) && k < 80; c += 0x20, k++) {
      const uint32_t req = T6Rd32(c + 8);
      std::fprintf(f, "  cancel %2u: cmd %08X %08X req %08X [%08X %08X %08X] extra %08X win %u-%u frame %u -> move %u opt %X\n", k, T6Rd32(c),
                   T6Rd32(c + 4), req, T6InGameRange(req, 12) ? T6Rd32(req) : 0, T6InGameRange(req, 12) ? T6Rd32(req + 4) : 0, T6InGameRange(req, 12) ? T6Rd32(req + 8) : 0, T6Rd32(c + 0xC),
                   T6Rd32(c + 0x10), T6Rd32(c + 0x14), T6Rd32(c + 0x18), T6Rd32(c + 0x1C) >> 16, T6Rd32(c + 0x1C) & 0xFFFF);
      if (T6Rd32(c) == 0x8000u) break;  // (end of the list in the T7 layout)
      // group reference (0x800B, the game's cancel processor sub_82286188): entries at
      // *(*(player + 0x100) + 0x1A4) + group * 32 up to 0x800C
      if (T6Rd32(c) == 0x800Bu) {
        const uint32_t ms = T6Rd32(pl + 0x100), gb = T6InGameRange(ms, 0x1A8) ? T6Rd32(ms + 0x1A4) : 0;
        for (uint32_t g = gb + (T6Rd32(c + 0x1C) >> 16) * 32u, j = 0; T6InGameRange(g, 0x20) && j < 40; g += 0x20, j++) {
          std::fprintf(f, "      group %u.%2u: cmd %08X %08X req %08X win %u-%u frame %u -> move %u opt %X\n", T6Rd32(c + 0x1C) >> 16, j, T6Rd32(g), T6Rd32(g + 4),
                       T6InGameRange(T6Rd32(g + 8), 4) ? T6Rd32(T6Rd32(g + 8)) : 0xFFFFFFFFu, T6Rd32(g + 0x10), T6Rd32(g + 0x14), T6Rd32(g + 0x18), T6Rd32(g + 0x1C) >> 16, T6Rd32(g + 0x1C) & 0xFFFF);
          if (T6Rd32(g) == 0x800Cu || T6Rd32(g) == 0x8000u) break;
        }
      }
    }
    if (T6Env("T6_MOVE_DUMP_RAW")) words(p, 0x100);
    for (uint32_t o = 0; T6Env("T6_MOVE_DUMP_RAW") && o < 0x100; o += 4) {
      const uint32_t q = T6Rd32(p + o);
      if (q != p && T6InGameRange(q, 0x60) && (q & 3) == 0) {
        std::fprintf(f, "  [+%02X] -> %08X\n", o, q);
        words(q, 0x60);
      }
    }
  }
  std::fflush(f);
}

// T6_FP_DUMP=<file> (research for the practice's frame data): every frame of a battle, each 32 bit word of the two
// fight player objects (*(0x828DCE88), *(0x828DCE8C), 0xC40 bytes) that changed: "<step> P<n> +<offset> old new".
void FightPlayerDump() {
  MoveStructDump();
  static const char* path = T6Env("T6_FP_DUMP");
  if (!path || !*path) return;
  static FILE* f = T6Open(path, "w");
  static uint8_t prev[2][0xC40];
  static uint32_t prev_at[2] = {};
  static uint32_t frame = 0;
  if (!f) return;
  frame++;
  for (int n = 0; n < 2; n++) {
    const uint32_t pl = T6PlayerPtr(n);
    if (!T6PlayerObjectOk(pl)) continue;
    const uint8_t* now = g_t6_membase + pl;
    if (prev_at[n] == pl) {
      for (uint32_t o = 0; o < 0xC40; o += 4) {
        if (std::memcmp(now + o, prev[n] + o, 4) != 0) {
          std::fprintf(f, "%u P%d +%03X %08X %08X\n", frame, n + 1, o, static_cast<unsigned>(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(prev[n] + o))),
                       static_cast<unsigned>(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(now + o))));
        }
      }
    } else {
      std::fprintf(f, "%u P%d object at %08X\n", frame, n + 1, pl);
      // (the whole object once: what the battle setup wrote before the first frame - customization research
      //  2026-10-14, e.g. a switch an item sets; lines "<frame> P<n> =<offset> <value>")
      for (uint32_t o = 0; o < 0xC40; o += 4)
        std::fprintf(f, "%u P%d =%03X %08X\n", frame, n + 1, o, static_cast<unsigned>(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(now + o))));
    }
    prev_at[n] = pl;
    std::memcpy(prev[n], now, 0xC40);
  }
  if (frame % 60 == 0) std::fflush(f);
}
