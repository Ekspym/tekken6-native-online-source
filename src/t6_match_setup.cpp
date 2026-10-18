// Tekken 6 game hooks - battle player setup, customization of a side, game info step (split out of t6_watch.cpp)

#include "t6_watch_internal.h"

// Battle player setup (vtable call): sub_82245090(this, side r4, character code r5) -> sub_8223F730(code) -> character
// id -> sub_82231158(player, id, ...). T6_CHAR_P1 / T6_CHAR_P2 = character code to use instead (StartMatch from the
// launcher); every call is logged.
extern "C" void __imp__sub_82245090(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8238E3B8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82245090(PPCContext& ctx, uint8_t* base) {
  const uint32_t side = ctx.r4.u32, code = ctx.r5.u32;
  const char* env = side == 0 ? T6Env("T6_CHAR_P1") : side == 1 ? T6Env("T6_CHAR_P2") : nullptr;
  if (env && *env) ctx.r5.u64 = std::strtoul(env, nullptr, 0);
  REXLOG_INFO("T6 match: player setup this {:08X} side {} code {} -> {} (lr {:08X})", ctx.r3.u32, side, code, ctx.r5.u32,
              static_cast<uint32_t>(ctx.lr));
  if (side == 0) {
    // match setup object (sub_8238E3B8): +344 round time (3600), +352 stage, +356 / +360 character codes.
    // T6_STAGE = stage id written before the battle uses it.
    PPCContext tmp = ctx;
    __imp__sub_8238E3B8(tmp, base);
    const uint32_t g = tmp.r3.u32;
    if (!g) {  // (no match setup object: nothing to write into)
      REXLOG_WARN("T6 match: no match setup object at the player setup");
      __imp__sub_82245090(ctx, base);
      return;
    }
    if (const char* st = T6Env("T6_STAGE"); st && *st && T6ToInt(st) >= 0) {
      *reinterpret_cast<uint32_t*>(base + g + 352) = __builtin_bswap32(std::strtoul(st, nullptr, 0));
    }
    // T6_ROUNDS = rounds needed to win the match (+282, s8; versus default 2), T6_ROUND_TIME = seconds (+344 in frames)
    const int rounds_before = T6Signed8(base[g + 282]);
    // +341 = most rounds of the match (sub_822CE348 ends it when the round counter reaches it): 2 * rounds - 1
    if (const char* r = T6Env("T6_ROUNDS"); r && T6ToInt(r) >= 1 && T6ToInt(r) <= 5) {
      base[g + 282] = static_cast<uint8_t>(T6ToInt(r));
      base[g + 341] = static_cast<uint8_t>(2 * T6ToInt(r) - 1);
    }
    if (const char* t = T6Env("T6_ROUND_TIME"); t && T6ToInt(t) >= 10) {
      *reinterpret_cast<uint32_t*>(base + g + 344) = __builtin_bswap32(static_cast<uint32_t>(T6ToInt(t)) * 60u);
    }
    REXLOG_INFO("T6 match: rounds to win {} -> {}", rounds_before, static_cast<int8_t>(base[g + 282]));
    std::string w;
    for (uint32_t o = 300; o < 460; o += 4) w += fmt::format(" +{}={}", o, T6Rd32(g + o));
    REXLOG_INFO("T6 match: setup object {:08X}:{}", g, w);
  }
  __imp__sub_82245090(ctx, base);
}


// Customization of a side (research 2026-10-14): sub_82244368(this, side r4, code r5, out u16* r6, out u16* r7,
// out mirror flag r8) returns the record the battle player setup passes on - the signed-in profile's record for
// that character and costume (profile + 352 + costume * 80), or 0. Layout: 21 u16 item ids, then 38 bytes.
// T6_CUSTOM_P1 / T6_CUSTOM_P2 = 160 hex digits (the 80 bytes as stored) -> returned for side 0 / 1 instead; the outs
// are set as for "no profile" (0xFFFF, 0xFFFF, mirror 0: no costume flip). Every call is logged.
extern "C" void __imp__sub_82244368(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82244368(PPCContext& ctx, uint8_t* base) {
  const uint32_t side = ctx.r4.u32, code = ctx.r5.u32, o1 = ctx.r6.u32, o2 = ctx.r7.u32, o3 = ctx.r8.u32;
  const char* env = side == 0 ? T6Env("T6_CUSTOM_P1") : side == 1 ? T6Env("T6_CUSTOM_P2") : nullptr;
  static uint32_t buf[2] = {};
  if (env && std::strlen(env) == 160 && g_t6_sys_alloc) {
    if (!buf[side]) buf[side] = g_t6_sys_alloc(80, 16);
    if (buf[side]) {
      for (int i = 0; i < 80; i++) {
        char hex[3] = {env[2 * i], env[2 * i + 1], 0};
        base[buf[side] + i] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
      }
      // (an out pointer the caller left 0 is not written: guest address 0 is not mapped)
      if (o1) *reinterpret_cast<uint16_t*>(base + o1) = 0xFFFF;
      if (o2) *reinterpret_cast<uint16_t*>(base + o2) = 0xFFFF;
      if (o3) *reinterpret_cast<uint32_t*>(base + o3) = 0;
      ctx.r3.u64 = buf[side];
      REXLOG_INFO("T6 custom: side {} code {} -> injected record {:08X}", side, code, buf[side]);
      return;
    }
  }
  __imp__sub_82244368(ctx, base);
  std::string rec;
  if (ctx.r3.u32)
    for (int i = 0; i < 80; i++) rec += fmt::format("{:02X}", base[ctx.r3.u32 + i]);
  REXLOG_INFO("T6 custom: side {} code {} -> game record {:08X} {}", side, code, ctx.r3.u32, rec);
}

// Game info object G (getter sub_8238E3B8: weak reference at 0x82735C70, pointer +16, serial +20 == G+12): character
// codes of the two sides at G+356 / G+360 (read by sub_823940C0 for the player setup). Logged every 100 pad steps and
// when they change; T6_CHAR_P1 / T6_CHAR_P2 (codes) are written there from T6_CHAR_FROM (pad step) on.
namespace t6w {
uint32_t GameInfo(uint8_t* base) {
  const uint32_t g = T6Rd32(0x82735C70u + 16);
  if (g < 0x40000000u || g >= 0xC0000000u) return 0;
  return T6Rd32(g + 12) == T6Rd32(0x82735C70u + 20) ? g : 0;
}
}  // namespace t6w
extern "C" void t6_game_info_step(uint8_t* base, uint32_t step) {
  const uint32_t g = GameInfo(base);
  if (!g) return;
  static uint32_t last1 = ~0u, last2 = ~0u;
  const uint32_t c1 = T6Rd32(g + 356), c2 = T6Rd32(g + 360);
  if (c1 != last1 || c2 != last2 || step % 500 == 0) {
    REXLOG_INFO("T6 match: game info {:08X} step {} codes P1 {} P2 {}", g, step, c1, c2);
    last1 = c1;
    last2 = c2;
  }
  if (!T6Env("T6_CHAR_TO_GAMEINFO")) return;  // G is not the match setup (gi1); kept for experiments
  static const uint32_t from = T6EnvU32("T6_CHAR_FROM", 0, 0);
  if (step < from) return;
  if (const char* p = T6Env("T6_CHAR_P1"); p && *p) *reinterpret_cast<uint32_t*>(base + g + 356) = __builtin_bswap32(std::strtoul(p, nullptr, 0));
  if (const char* p = T6Env("T6_CHAR_P2"); p && *p) *reinterpret_cast<uint32_t*>(base + g + 360) = __builtin_bswap32(std::strtoul(p, nullptr, 0));
}
