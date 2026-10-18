// Tekken 6 game hooks - the launcher's Customize session (free items, money, item lists) (split out of t6_watch.cpp)

#include "t6_watch_internal.h"

// T6_FREE_ITEMS=1 (the Customize session): every 2 s the committed guest memory is searched for customization
// items ("IB03", +8 u16 id, +12 price) and their price set to 0 - the item shop sells everything for 0 G
static void T6FreeItems(uint8_t* base) {
  static uint32_t frame = 0;
  static const bool watching = T6Env("T6_WATCH_ITEM") != nullptr;
  if (frame++ % (watching ? 5 : 120) != 0) return;
  uint32_t n = 0;
  MEMORY_BASIC_INFORMATION mi;
  for (uint8_t* a = base + 0xA0000000ull; a < base + 0xC0000000ull; a += mi.RegionSize) {
    if (!VirtualQuery(a, &mi, sizeof(mi)) || !mi.RegionSize) break;
    if (mi.State != MEM_COMMIT || !(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (mi.Protect & PAGE_GUARD)) continue;
    uint8_t* end = static_cast<uint8_t*>(mi.BaseAddress) + mi.RegionSize;
    if (end > base + 0xC0000000ull) end = base + 0xC0000000ull;
    for (uint8_t* q = static_cast<uint8_t*>(mi.BaseAddress); q + 16 <= end; q += 4) {
      if (*reinterpret_cast<const uint32_t*>(q) != 0x33304249u) continue;  // "IB03"
      const uint32_t id = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(q + 8)) & 0xFFFF;
      uint32_t& price = *reinterpret_cast<uint32_t*>(q + 12);
      if (id && id < 4096 && price && __builtin_bswap32(price) < 100000000u) {
        price = 0;
        n++;
        // T6_WATCH_ITEM=<n> (research): the n-th item with id 1 found is watched (who reads which field)
        static int ones = 0;
        if (id == 1) {
          REXLOG_INFO("T6 free items: id 1 #{} at {:08X}", ones, static_cast<uint32_t>(q - base));
          if (const char* wi = T6Env("T6_WATCH_ITEM"); wi && T6ToInt(wi) == ones) CwStart(base, static_cast<uint32_t>(q - base));
          ones++;
        }
      }
    }
  }
  if (n) REXLOG_INFO("T6 free items: {} prices set to 0", n);
}

// Game save object S = sub_8243E430() (research 2026-10-15): +244 fight money (sub_823F1DE8, read by the item
// shop), +26220 the item table of the shop (sub_823F2518). T6_MONEY=<gold> keeps +244 at that.
uint32_t g_t6_save_obj = 0;
extern "C" void __imp__sub_8243E430(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8243E430(PPCContext& ctx, uint8_t* base) {
  __imp__sub_8243E430(ctx, base);
  if (ctx.r3.u32 && ctx.r3.u32 != g_t6_save_obj) {
    g_t6_save_obj = ctx.r3.u32;
    REXLOG_INFO("T6 save object: S {:08X} (+80 {} +84 {} +88 {})", ctx.r3.u32, base[ctx.r3.u32 + 80],
                T6Rd32(ctx.r3.u32 + 84), T6Rd32(ctx.r3.u32 + 88));
    // (research) T6_WATCH_S=<offset>,<length> of S (default the first 0x200 bytes)
    if (const char* ws = T6Env("T6_WATCH_S")) {
      char* e = nullptr;
      const uint32_t off = std::strtoul(ws, &e, 10), len = *e == ',' ? std::strtoul(e + 1, nullptr, 10) : 0x200;
      CwStart(base, ctx.r3.u32 + off, len);
    }
  }
}

// Customize item list of a character model (shop object r3, research 2026-10-15): sub_823F4028 builds +140 first
// entry, +148 count, +168 container (entry = 8 bytes, +4 offset of the item from the container) for character code
// +128. Owned items = the character's inventory in the save object: S + 26220 + (index * 1500 + slot) * 12 (index =
// sub_823F6100(code): the row of the 41 x 3 code table at 0x827AB928), slot: +4 u16 item (position in that list),
// +8 0xFF, +10 bit 0 used (bit 1 new) - what a purchase (sub_823F4E88) writes. T6_OWN_ALL=1: after the list is built,
// every item of it not in the inventory yet is added (all items owned, in the Equip lists).
extern "C" void __imp__sub_823F4028(PPCContext& ctx, uint8_t* base);
extern "C" void sub_823F4028(PPCContext& ctx, uint8_t* base) {
  const uint32_t shop = ctx.r3.u32;
  __imp__sub_823F4028(ctx, base);
  static const bool own = T6Env("T6_OWN_ALL") != nullptr;
  if (!own || !shop || !g_t6_save_obj) return;
  const uint32_t code = T6Rd32(shop + 128), first = T6Rd32(shop + 140), count = T6Rd32(shop + 148), cont = T6Rd32(shop + 168);
  int ci = -1;
  for (int r = 0; r < 41 && ci < 0; r++)
    for (int k = 0; k < 3; k++)
      if (T6Rd32(0x827AB928u + r * 12 + k * 4) == code) { ci = r; break; }
  if (ci < 0 || !cont || count == 0 || count > 1489) return;
  const uint32_t inv = g_t6_save_obj + 26220 + static_cast<uint32_t>(ci) * 1500 * 12;
  std::vector<bool> have(count, false);
  for (uint32_t k = 0; k < 1489; k++)
    if (base[inv + k * 12 + 10] & 1) {
      const uint16_t it = static_cast<uint16_t>(base[inv + k * 12 + 4] << 8 | base[inv + k * 12 + 5]);
      if (it < count) have[it] = true;
    }
  {
    std::string u;
    int shown = 0;
    for (uint32_t k2 = 0; k2 < 1489 && shown < 4; k2++)
      if (base[inv + k2 * 12 + 10] & 1) {
        u += fmt::format(" [{}]", k2);
        for (int j = 0; j < 12; j++) u += fmt::format("{:02X}", base[inv + k2 * 12 + j]);
        shown++;
      }
    u += " free0 ";
    for (int j = 0; j < 12; j++) u += fmt::format("{:02X}", base[inv + j]);
    REXLOG_INFO("T6 own all: row {} before:{}", ci, u);
  }
  uint32_t added = 0, k = 0;
  uint32_t base_parts = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (have[i]) continue;
    // the 1P / 2P base parts (+16 = 0x0100 / 0x0200) are the defaults, never items of the inventory
    const uint32_t item = cont + T6Rd32(cont + (first + i) * 8 + 4);
    if (base[item + 16] != 0) { base_parts++; continue; }
    if (static const uint32_t cap = T6EnvU32("T6_OWN_MAX", 9999, 10); added >= cap) break;
    while (k < 1489 && (base[inv + k * 12 + 10] & 1)) k++;
    if (k >= 1489) break;
    uint8_t* e = base + inv + k * 12;
    e[4] = static_cast<uint8_t>(i >> 8);
    e[5] = static_cast<uint8_t>(i);
    e[8] = 0xFF;
    e[10] |= 1;
    added++;
  }
  REXLOG_INFO("T6 own all: shop {:08X} code {} row {} items {} (first {}, container {:08X}) - {} added, {} base parts left out", shop,
              code, ci, count, first, cont, added, base_parts);
}

// ---- The launcher's Customize session (T6_CUSTOM_CHAR / T6_CUSTOM_OUT, 2026-10-15) ----
// The editor works on the profile's P+352 (3 models x 80 bytes of the chosen character); leaving it writes them back
// to the save object, S + 13876 + character id * 240 (sub_824459B8), and leaving the Customize menu saves the game.
uint32_t g_t6_custom_gid = UINT32_MAX;  // the character id the editor was opened with
uint8_t g_t6_custom_start[240];         // its 3 models when the editor opened

// "<T6_CUSTOM_OUT>.state": "customize ready" (the editor is open: the launcher shows the window) / "customize done"
// (its own file: the status writer rewrites T6_NET_STATUS all the time)
void T6CustomStatus(const char* line) {
  const char* out = T6Env("T6_CUSTOM_OUT");
  if (!out || !*out) return;
  T6WriteFileAtomic(std::string(out) + ".state", std::string(line) + "\n");
}

// every frame of the Customize session (pad hook), task = the Customize menu (task 8208B714) exists: once it was there
// and is gone (the player left it, the game saves), 240 frames later the chosen character's 3 models go to
// T6_CUSTOM_OUT ("char <id>", "code <code> model <index>" for the P1 / P2 code, "model <i> <160 hex digits>", "start
// <i> <160 hex digits>" = that model when the editor opened) and the
// game closes
void T6CustomizeTick(uint8_t* base, bool task) {
  static bool seen = false;
  static int left = -1;
  if (task) { seen = true; left = -1; return; }
  if (!seen || g_t6_custom_gid == UINT32_MAX) return;
  if (left < 0) {
    left = 240;
    REXLOG_INFO("T6 customize: left the Customize menu, closing after the save");
  }
  if (--left > 0) return;
  const char* out = T6Env("T6_CUSTOM_OUT");
  const uint32_t rec = g_t6_save_obj ? g_t6_save_obj + 13876 + g_t6_custom_gid * 240 : 0;
  if (out && *out && rec) {
    {
      std::string f = fmt::format("char {}\n", g_t6_custom_gid);
      if (const char* cc = T6Env("T6_CUSTOM_CHAR"); cc && *cc)
        for (const uint32_t c : {static_cast<uint32_t>(T6ToInt(cc)) & ~1u, static_cast<uint32_t>(T6ToInt(cc)) | 1u})
          if (c < 102)  // sub_822F39C0: the model index of a code
            f += fmt::format("code {} model {}\n", c, T6Rd32(0x82744AA8u + c * 108 + 4));
      // the P3 code of the character: third column of the row of the code table 0x827AB928 (41 x 3, 102 = none) that
      // holds its P1 / P2 code (the row is not the character id: LEE id 15 = row 16)
      if (const char* cc = T6Env("T6_CUSTOM_CHAR"); cc && *cc) {
        const uint32_t p1 = static_cast<uint32_t>(T6ToInt(cc)) & ~1u;
        const auto rd = [&](uint32_t i) { return T6Rd32(0x827AB928u + i * 4); };
        for (uint32_t r = 0; r < 41; r++)
          if ((rd(r * 3) == p1 || rd(r * 3 + 1) == (p1 | 1u)) && rd(r * 3 + 2) < 102) {
            const uint32_t c = rd(r * 3 + 2);
            f += fmt::format("code {} model {}\n", c, T6Rd32(0x82744AA8u + c * 108 + 4));
            break;
          }
      }
      for (uint32_t m = 0; m < 3; m++) {
        f += fmt::format("model {} ", m);
        for (uint32_t i = 0; i < 80; i++) f += fmt::format("{:02X}", base[rec + m * 80 + i]);
        f += fmt::format("\nstart {} ", m);
        for (uint32_t i = 0; i < 80; i++) f += fmt::format("{:02X}", g_t6_custom_start[m * 80 + i]);
        f += '\n';
      }
      T6WriteFileAtomic(out, f);
      REXLOG_INFO("T6 customize: character {} written to {}", g_t6_custom_gid, out);
    }
  }
  T6CustomStatus("customize done");
  ExitProcess(0);
}

// Customize: a character chosen (sub_824591F8(customize object r3), research 2026-10-15): its grid index +7336 ->
// code = the table 0x827C0C58[index] -> profile P+596, the character's 3 models (240 bytes) copied to P+352.
// T6_CUSTOM_CHAR=<code>: the index of that code is put in +7336 first (the launcher's character, whatever the cursor)
extern "C" void __imp__sub_824591F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822F39F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824591F8(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  if (const char* cc = T6Env("T6_CUSTOM_CHAR"); cc && *cc && obj) {
    // the launcher's character code (kCharCodes) -> the game's character id (sub_822F39F8, as sub_82244368 does) ->
    // the first grid index whose table entry is that id (from 1: entry 0 is 0 too and no PAUL cell - PAUL got index 0
    // and the game crashed, players 2026-10-17; PAUL = 1, LAW = 2 ... KUMA / PANDA 12 / 13 both id 11)
    PPCContext c2 = ctx;
    c2.r3.u64 = static_cast<uint32_t>(T6ToInt(cc));
    sub_822F39F8(c2, base);
    const uint32_t gid = c2.r3.u32;
    for (uint32_t i = 1; i < 48; i++)
      if (T6Rd32(0x827C0C58u + i * 4) == gid) {
        *reinterpret_cast<uint32_t*>(base + obj + 7336) = __builtin_bswap32(i);
        REXLOG_INFO("T6 customize: code {} -> id {} -> grid index {}", T6ToInt(cc), gid, i);
        break;
      }
  }
  REXLOG_INFO("T6 customize: character index {} table {}", T6Rd32(obj + 7336), T6Rd32(0x827C0C58u + T6Rd32(obj + 7336) * 4));
  static bool dumped = false;
  if (!dumped) {
    dumped = true;
    std::string t;
    for (uint32_t i = 0; i < 48; i++) t += fmt::format(" {}", T6Rd32(0x827C0C58u + i * 4));
    REXLOG_INFO("T6 customize: table 827C0C58:{}", t);
    t.clear();
    for (uint32_t i = 0; i < 41 * 3; i++) t += fmt::format("{}{}", i % 3 ? "," : " ", T6Rd32(0x827AB928u + i * 4));
    REXLOG_INFO("T6 customize: rows 827AB928:{}", t);
  }
  __imp__sub_824591F8(ctx, base);
  g_t6_custom_gid = T6Rd32(0x827C0C58u + T6Rd32(obj + 7336) * 4);
  // the character's 3 models as the editor found them (the launcher keeps only the models changed in the session)
  if (static bool kept = false; !kept && g_t6_save_obj) {
    kept = true;
    std::memcpy(g_t6_custom_start, base + g_t6_save_obj + 13876 + g_t6_custom_gid * 240, 240);
  }
  T6CustomStatus("customize ready");
  {
    std::lock_guard lock(g_t6_profiles_mutex);
    for (const uint32_t p : g_t6_profiles) REXLOG_INFO("T6 customize: P {:08X} +596 = {}", p, base[p + 596]);
  }
}

// every frame (pad hook): the profiles seen get T6_MONEY again (the save load overwrites it after the first look)
void T6KeepMoney(uint8_t* base) {
  static const bool free_items = T6Env("T6_FREE_ITEMS") != nullptr;
  if (free_items) T6FreeItems(base);
  static const char* money = T6Env("T6_MONEY");
  if (!money || !*money || T6Env("T6_WATCH_P")) return;
  const uint32_t v = __builtin_bswap32(static_cast<uint32_t>(std::strtoul(money, nullptr, 10)));
  if (g_t6_save_obj) *reinterpret_cast<uint32_t*>(base + g_t6_save_obj + 244) = v;  // fight money (sub_823F1DE8)
  // T6_UNLOCK_ALL=1: the 289 unlock states (4 bits each, even id low nibble, at S + 0xC0000 - 10404; sub_82494BA0:
  // 0 = decided by its condition, 4 = locked, else unlocked) all set to 3
  static const bool unlock = T6Env("T6_UNLOCK_ALL") != nullptr;
  if (unlock && g_t6_save_obj) std::memset(base + g_t6_save_obj + 0xC0000 - 10404, 0x33, 145);
  std::lock_guard lock(g_t6_profiles_mutex);
  for (const uint32_t p : g_t6_profiles) *reinterpret_cast<uint32_t*>(base + p + 180) = v;

}
