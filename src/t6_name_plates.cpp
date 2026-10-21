// Tekken 6 game hooks - name plates of the fight HUD (split out of t6_watch.cpp)

#include "t6_watch_internal.h"

// Name plates of the fight HUD (tcb_kkProfile, vtable 0x82003C6C, 0x380 bytes, one per side; +0x58 side byte, +0x88
// "has a name" flag, +0x89 the name - ANSI, as XamUserGetName returned it, 16 bytes with the terminator). The battle
// HUD (sub_82246400, per side) creates them through sub_821C53A0 (the signed-in user's profile), sub_821C45C0,
// sub_821C5620 or sub_821C56F8 (player info given), manager r3, side r4; the manager keeps a weak reference to the
// plate of a side at manager + 144 + side * 24. Offline only the signed-in user's side has a name, so the other side
// looks like a guest / the computer. Online both plates get the names the launcher passed (T6_NAME_P1 / T6_NAME_P2,
// 'name1=' / 'name2=' of the start line; the same on both instances, so the state stays equal).
extern "C" unsigned char t6_dirty[0x100000];
extern "C" void t6_dirty_mark(uint32_t a);
namespace t6w {
uint32_t g_plate_mgr = 0;  // HUD plate manager (heap object, the same address on both instances)
void T6NamePlate(uint8_t* base, uint32_t mgr, uint32_t side, const char* from) {
  const uint32_t plate = (mgr >= 0x82000000u && side < 4) ? T6Rd32(mgr + 144 + side * 24) : 0;
  const bool ok = plate >= 0x82000000u && T6Rd32(plate) == 0x82003C6Cu;
  const char* name = side == 0 ? T6Env("T6_NAME_P1") : side == 1 ? T6Env("T6_NAME_P2") : nullptr;
  if (from) g_plate_mgr = mgr;
  // (logged once per plate and side: it was called every few ms while a plate is up - half of a player's log, 2026-10-17)
  // (and at most 8 lines per side per run: the HUD cycles through 5+ plate objects every few ms, so "a new plate" was
  // true at almost every call - 4577 of 9579 lines of honk's log, 2026-10-20)
  static uint32_t logged_plate[4] = {};
  static uint32_t logged_lines[4] = {};
  if (from && side < 4 && logged_plate[side] != plate && logged_lines[side] < 8) {
    logged_plate[side] = plate;
    REXLOG_INFO("T6 match: name plate from {}: side {} manager {:08X} plate {:08X}{}{}", from, side, mgr, plate,
                ok ? fmt::format(" flag {} name '{}' side byte {}", base[plate + 0x88],
                                 std::string(reinterpret_cast<const char*>(base + plate + 0x89), strnlen(reinterpret_cast<const char*>(base + plate + 0x89), 15)),
                                 base[plate + 0x58]) : std::string(" (no plate)"),
                ++logged_lines[side] == 8 ? " (the last name plate line of this side in this run)" : "");
  }
  // Rank byte of the plate (+597, copy +660; rk1: +596 next to it changed nothing in the HUD - sub_82441438 fills
  // +596 / +597 from two per-character byte arrays of the save and copies +597 to +660, and the plate's own code
  // sub_821C0E40 / 0E90 / 11E0 / 15C0 reads +597) from the start line's rank1= / rank2= (T6_RANK_P1 / T6_RANK_P2, the
  // same on both instances); written again whenever the plate holds another value
  if (ok && side < 2) {
    if (const char* r = T6Env(side == 0 ? "T6_RANK_P1" : "T6_RANK_P2"); r && *r) {
      const uint8_t v = static_cast<uint8_t>(T6ToInt(r));
      if (base[plate + 597] != v || base[plate + 660] != v) {
        base[plate + 597] = base[plate + 660] = v;
        t6_dirty_mark(plate + 597);
        t6_dirty_mark(plate + 660);
      }
    }
  }
  if (!ok || !name || !*name) return;
  char buf[16];
  T6PlateName(name, buf);
  // Side 1 (offline a guest plate: kind 1, "Player2"): drawn like the signed-in user's plate (kind 0: rank, picture,
  // name; nm5) with name flag 2. The game's name getter sub_824424F0 (block = plate + 96: xuid +0, flag +40, name
  // +41) looks the gamertag up by xuid when the flag is 1 - that put the LOCAL account's name on both sides (nm5 /
  // nm6) - copies the default text ("Player2") when it is 0, and returns the stored name as it is for any other
  // value. The rank byte (+596, copy +660) is taken from side 0's plate until the launcher passes ranks.
  const uint8_t flag = side == 1 ? static_cast<uint8_t>(T6EnvInt("T6_PLATE_FLAG", 2)) : 1;
  if (base[plate + 0x88] == flag && std::memcmp(base + plate + 0x89, buf, sizeof(buf)) == 0) return;
  if (!from) {
    static int logged = 0;
    if (const int k = logged++; k < 40 && (k < 5 || side == 1)) {
      REXLOG_INFO("T6 match: name plate side {} plate {:08X}: flag {} name '{}' -> '{}'", side, plate, base[plate + 0x88],
                  std::string(reinterpret_cast<const char*>(base + plate + 0x89), strnlen(reinterpret_cast<const char*>(base + plate + 0x89), 15)), buf);
    }
  }
  base[plate + 0x88] = flag;
  std::memcpy(base + plate + 0x89, buf, sizeof(buf));
  // T6_PLATE_KIND = kind byte of side 1's plate (+0x58; 0 profile, 1 guest, 3 other player data: rank + yellow name,
  // no picture), T6_PLATE_RANK = its rank byte
  if (side == 1) {
    const char* k = T6Env("T6_PLATE_KIND");
    base[plate + 0x58] = static_cast<uint8_t>(k && *k ? T6ToInt(k) : 0);
    const uint32_t p0 = T6Rd32(mgr + 144);
    uint8_t rank = (p0 >= 0x82000000u && T6Rd32(p0) == 0x82003C6Cu) ? base[p0 + 597] : 0;
    if (const char* r = T6Env("T6_PLATE_RANK"); r && *r) rank = static_cast<uint8_t>(T6ToInt(r));
    if (const char* r = T6Env("T6_RANK_P2"); r && *r) rank = static_cast<uint8_t>(T6ToInt(r));
    base[plate + 597] = base[plate + 660] = rank;
  }
  for (uint32_t a : {plate + 0x58, plate + 0x88, plate + 0x99, plate + 597, plate + 660}) t6_dirty_mark(a);
}
}  // namespace t6w
// Every live frame boundary of a session: the game fills the name of a profile plate after its creation (the
// signed-in user's gamertag), so the launcher's names are written again whenever a plate holds something else.
void T6NamePlatesFrame(uint8_t* base) {
  // The signed-in user's gamertag as the account manager keeps it (CAccountManagerDriver_X360 0x8297BBF8, +0x20,
  // 16 bytes, from XamUserGetName): the game copies it into side 0's plate every frame (nm3: 'User' came back after
  // every write to the plate), so player 1's name goes there. T6_NAME_ACCOUNT=0 = leave it.
  if (const char* n = T6Env("T6_NAME_P1"); n && *n && T6Rd32(0x8297BBF8u) == 0x82083038u) {
    static const bool on = T6EnvDefaultOn("T6_NAME_ACCOUNT");
    char buf[16];
    T6PlateName(n, buf);
    if (on && std::memcmp(base + 0x8297BC18u, buf, sizeof(buf)) != 0) {
      std::memcpy(base + 0x8297BC18u, buf, sizeof(buf));
      t6_dirty_mark(0x8297BC18u);
      t6_dirty_mark(0x8297BC27u);
    }
  }
  if (!g_plate_mgr) return;
  for (uint32_t side = 0; side < 2; side++) T6NamePlate(base, g_plate_mgr, side, nullptr);
}
#define T6_PLATE_HOOK(fn)                                                extern "C" void __imp__##fn(PPCContext& ctx, uint8_t* base);           extern "C" void fn(PPCContext& ctx, uint8_t* base) {                     const uint32_t mgr = ctx.r3.u32, side = ctx.r4.u32;                    __imp__##fn(ctx, base);                                                T6NamePlate(base, mgr, side, #fn);                                   }
T6_PLATE_HOOK(sub_821C53A0)
T6_PLATE_HOOK(sub_821C45C0)
T6_PLATE_HOOK(sub_821C5620)
T6_PLATE_HOOK(sub_821C56F8)
