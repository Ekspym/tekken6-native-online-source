// Tekken 6 game hooks - research hooks of the customization (reads, items, save data, profiles) (split out of t6_watch.cpp)

#include "t6_watch_internal.h"

// T6_CUSTOM_WATCH=1 (research 2026-10-15): which guest functions read a player's customization record copy. The host
// page(s) of the record are made PAGE_NOACCESS; every access violation there is let through once (page opened, single
// step, page closed again) and an access inside the 80 bytes is logged once per guest function (host rip -> the
// nearest lower host function of PPCFuncMappings), with the record offset and the guest lr.
namespace t6w {
CustWatch g_cw;
thread_local uint8_t* t_cw_rearm = nullptr;

uint32_t CwGuestFunc(uintptr_t rip) {
  auto it = std::upper_bound(g_cw.funcs.begin(), g_cw.funcs.end(), std::make_pair(rip, UINT32_MAX));
  return it == g_cw.funcs.begin() ? 0 : std::prev(it)->second;
}

LONG WINAPI CwHandler(EXCEPTION_POINTERS* ep) {
  const DWORD code = ep->ExceptionRecord->ExceptionCode;
  if (code == EXCEPTION_SINGLE_STEP && t_cw_rearm) {
    DWORD old;
    VirtualProtect(t_cw_rearm, 0x1000, PAGE_NOACCESS, &old);
    t_cw_rearm = nullptr;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (code != EXCEPTION_ACCESS_VIOLATION || !g_cw.lo) return EXCEPTION_CONTINUE_SEARCH;
  auto* a = reinterpret_cast<uint8_t*>(ep->ExceptionRecord->ExceptionInformation[1]);
  if (a < g_cw.lo || a >= g_cw.hi) return EXCEPTION_CONTINUE_SEARCH;
  if (a >= g_cw.rec && a < g_cw.rec + g_cw.len) {
    const uintptr_t rip = ep->ContextRecord->Rip;
    const uint32_t fn = CwGuestFunc(rip), off = static_cast<uint32_t>(a - g_cw.rec);
    bool first;
    {
      std::lock_guard lock(g_cw.m);
      // (a large range: once per function and 16-byte block, at most 4000 lines)
      first = g_cw.seen.insert({fn, g_cw.len > 80 ? off & ~15u : off}).second && g_cw.logged++ < 4000;
    }
    if (first) {
      using CtxFn = void* (*)();
      static CtxFn cf = reinterpret_cast<CtxFn>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "rex_current_guest_context"));
      auto* c = cf ? static_cast<PPCContext*>(cf()) : nullptr;
      REXLOG_INFO("T6 custom watch: record {:08X} +{} {} by sub_{:08X} (host rip {:X}) guest lr {:08X} r3 {:08X}", g_cw.guest, off,
                  ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "read", fn, rip, c ? static_cast<uint32_t>(c->lr) : 0,
                  c ? c->r3.u32 : 0);
    }
  }
  DWORD old;
  uint8_t* page = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(a) & ~uintptr_t(0xFFF));
  VirtualProtect(page, 0x1000, PAGE_READWRITE, &old);
  t_cw_rearm = page;
  ep->ContextRecord->EFlags |= 0x100;  // trap flag: close the page again after this one instruction
  return EXCEPTION_CONTINUE_EXECUTION;
}

void CwStart(uint8_t* base, uint32_t rec, uint32_t len) {
  if (g_cw.lo || !rec) return;  // the first battle record only
  g_cw.len = len;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host; m++)
    g_cw.funcs.emplace_back(reinterpret_cast<uintptr_t>(m->host), static_cast<uint32_t>(m->guest));
  std::sort(g_cw.funcs.begin(), g_cw.funcs.end());
  g_cw.rec = base + rec;
  g_cw.guest = rec;
  g_cw.lo = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(g_cw.rec) & ~uintptr_t(0xFFF));
  g_cw.hi = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(g_cw.rec + len) + 0xFFF) & ~uintptr_t(0xFFF));
  AddVectoredExceptionHandler(1, CwHandler);
  DWORD old;
  VirtualProtect(g_cw.lo, g_cw.hi - g_cw.lo, PAGE_NOACCESS, &old);
  REXLOG_INFO("T6 custom watch: record {:08X} watched ({} host functions)", rec, g_cw.funcs.size());
}

void CwStop() {
  if (!g_cw.lo) return;
  DWORD old;
  VirtualProtect(g_cw.lo, g_cw.hi - g_cw.lo, PAGE_READWRITE, &old);
  g_cw.lo = g_cw.hi = nullptr;
  REXLOG_INFO("T6 custom watch: stopped");
}
}  // namespace t6w


// Player character set (sub_82231158(player r3, character id r4, ...)): logged with the caller (StartMatch research).
// r9 = the 80-byte customization record of that player (0 = default look, sub_8222dde8 zeroes the player's copy).
extern "C" void __imp__sub_82231158(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82231158(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("T6 match: set character player {:08X} id {} r5 {} r6 {} custom {:08X} (lr {:08X}, step {})", ctx.r3.u32, ctx.r4.u32,
              ctx.r5.u32, ctx.r6.u32, ctx.r9.u32, static_cast<uint32_t>(ctx.lr), g_step);
  const uint32_t player = ctx.r3.u32, id = ctx.r4.u32;
  __imp__sub_82231158(ctx, base);
  // T6_CUSTOM_DUMP=1 (research): after a battle character set (not the placeholder id 83) the object O = *(player + 88),
  // its first 0x100 bytes, and the player's record copy at *(O + 4) (80 bytes)
  if (static const bool dump = T6Env("T6_CUSTOM_DUMP") != nullptr; dump && id != 83 && player) {
    const uint32_t o = T6Rd32(player + 88);
    std::string so, sr;
    if (o) {
      for (uint32_t i = 0; i < 0x100; i++) so += fmt::format("{:02X}", base[o + i]);
      if (const uint32_t r = T6Rd32(o + 4))
        for (uint32_t i = 0; i < 80; i++) sr += fmt::format("{:02X}", base[r + i]);
    }
    REXLOG_INFO("T6 custom dump: player {:08X} id {} O {:08X} O[0x100] {} record {}", player, id, o, so, sr);
    if (static const bool watch = T6Env("T6_CUSTOM_WATCH") != nullptr; watch && o) CwStart(base, T6Rd32(o + 4));
  }
}

// T6_CUSTOM_ITEMS=1 (research 2026-10-15): sub_822340F8(obj r3, record r4) builds a player's custom look: entries
// 0..21 = the u16 ids at +0..+43, entry 22 = the u16 at +53/+54; each non-zero id -> sub_822338E0(obj, id) = the item
// whose u16 at +8 is that id, among the entries [*(obj+1952), +*(obj+1948)) of the container *(obj+1888) (header word
// < 10: entries at container + *(container+4), else at container; entry = 8 bytes, +4 = offset of the item from there).
// Bytes +44..46 = colors. Logs every item of that container once per object (id, the item's first 64 bytes).
extern "C" void __imp__sub_822340F8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822340F8(PPCContext& ctx, uint8_t* base) {
  static const bool items = T6Env("T6_CUSTOM_ITEMS") != nullptr;
  static std::set<uint32_t> done;
  const uint32_t obj = ctx.r3.u32;
  if (items && obj && done.insert(obj).second) {
    const uint32_t tbl = T6Rd32(obj + 1888), first = T6Rd32(obj + 1952), n = T6Rd32(obj + 1948);
    REXLOG_INFO("T6 custom items: obj {:08X} container {:08X} entries {} first {} count {} (+1892 {})", obj, tbl,
                T6Rd32(obj + 1944), first, n, T6Rd32(obj + 1892));
    if (tbl && n < 4096) {
      const uint32_t eb = T6Rd32(tbl) < 10 ? tbl + T6Rd32(tbl + 4) : tbl;
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t it = eb + T6Rd32(eb + (first + i) * 8 + 4);
        std::string h;
        for (uint32_t k = 0; k < 64; k++) h += fmt::format("{:02X}", base[it + k]);
        // the two pointers at +32 / +36: 48 bytes each, printable characters as they are, the rest as '.'
        std::string p[2];
        for (int j = 0; j < 2; j++)
          if (const uint32_t q = T6Rd32(it + 32 + 4 * j); q >= 0x40000000u && q < 0xC0000000u)
            for (uint32_t k = 0; k < 48; k++) {
              const uint8_t c = base[q + k];
              p[j] += c >= 32 && c < 127 ? static_cast<char>(c) : '.';
            }
        REXLOG_INFO("T6 custom item: obj {:08X} #{} id {} at {:08X} {} | {} | {}", obj, i, T6Rd32(it + 8) & 0xFFFF, it, h, p[0], p[1]);
      }
    }
  }
  __imp__sub_822340F8(ctx, base);
}

// Save data check (research 2026-10-15): sub_82440008(save r3) clamps the loaded save's fields (+80 level < 101,
// +84 / +88 fight money <= 765000000, per-character arrays of 43 from +24207 ...). Logged with the address;
// T6_MONEY=<gold> writes +84 after it (the Customize menu buys with it).
uint32_t g_t6_save = 0;
extern "C" void __imp__sub_82440008(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82440008(PPCContext& ctx, uint8_t* base) {
  const uint32_t save = ctx.r3.u32;
  __imp__sub_82440008(ctx, base);
  g_t6_save = save;
  REXLOG_INFO("T6 save check: save {:08X} +84 {} +88 {} +92 {} +96 {}", save, T6Rd32(save + 84), T6Rd32(save + 88), T6Rd32(save + 92), T6Rd32(save + 96));
  if (const char* m = T6Env("T6_MONEY"); m && *m)
    *reinterpret_cast<uint32_t*>(base + save + 84) = __builtin_bswap32(static_cast<uint32_t>(std::strtoul(m, nullptr, 10)));
}

// Player profile of a side (research 2026-10-15): sub_821C3E80(manager *(G+932), side) = P; +88 sign-in state, +180
// fight money (+184 money earned in total, both <= 765000000), +352 the 3 custom models (80 bytes each) of the main
// character +596 (sub_82244368). T6_MONEY=<gold>: P+180 kept at that (the Customize menu buys with it).
std::set<uint32_t> g_t6_profiles;
std::mutex g_t6_profiles_mutex;
extern "C" void __imp__sub_821C3E80(PPCContext& ctx, uint8_t* base);
extern "C" void sub_821C3E80(PPCContext& ctx, uint8_t* base) {
  __imp__sub_821C3E80(ctx, base);
  static const char* money = T6Env("T6_MONEY");
  if (money && *money && ctx.r3.u32) {
    std::lock_guard lock(g_t6_profiles_mutex);
    if (g_t6_profiles.insert(ctx.r3.u32).second) {
      REXLOG_INFO("T6 profile: P {:08X} (money {})", ctx.r3.u32, T6Rd32(ctx.r3.u32 + 180));
      if (T6Env("T6_WATCH_P")) CwStart(base, ctx.r3.u32, 0x800);  // (research: who reads / writes the money)
    }
  }
}
