// Tekken 6 netplay - host crash report with the guest registers (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- Host crash report with the guest registers (any access violation in generated code) ----
namespace t6np {
// Host threads (audio worker, GPU command processor, net worker) and the other fatal codes get a line too: players'
// games ended in the middle of a session with nothing in the log (FamouslyUnknown 2026-10-20 and 11 more in the
// uploaded logs) - this handler only knew access violations on threads with a guest context. Host rip as module +
// offset (the exe offset maps to the generated function, scratchpad whichfn2.py). (__fastfail never reaches here.)
static bool FatalCode(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_IN_PAGE_ERROR:
    case 0xC0000374u:  // heap corruption
    case 0xC0000409u:  // stack buffer overrun
      return true;
    default:
      return false;
  }
}
static void LogHostException(EXCEPTION_POINTERS* ep) {
  const DWORD64 rip = ep->ContextRecord->Rip;
  HMODULE mod = nullptr;
  char path[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(rip), &mod) && mod)
    GetModuleFileNameA(mod, path, sizeof(path));
  const char* slash = std::strrchr(path, '\\');
  const char* file = slash ? slash + 1 : path;
  REXLOG_ERROR("T6 crash: exception {:08X} on thread {} at {}+{:X} (host rip {:016X}), addr {:016X}{}",
               static_cast<uint32_t>(ep->ExceptionRecord->ExceptionCode), GetCurrentThreadId(), file,
               mod ? rip - reinterpret_cast<DWORD64>(mod) : 0, rip,
               static_cast<uint64_t>(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0),
               T6Rex().current_guest_context && T6Rex().current_guest_context() ? "" : " (no guest context: a host thread)");
}
LONG WINAPI T6CrashHandler(EXCEPTION_POINTERS* ep) {
  if (!FatalCode(ep->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
  static std::atomic<int> once{0};
  if (once.fetch_add(1) > 3) return EXCEPTION_CONTINUE_SEARCH;
  LogHostException(ep);
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  const auto fn = T6Rex().current_guest_context;
  if (auto* c = fn ? static_cast<PPCContext*>(fn()) : nullptr) {
    REXLOG_ERROR("T6 crash: host rip {:016X} addr {:016X}; guest lr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r30 {:08X} r31 {:08X} last_indirect {:08X}",
                 static_cast<uint64_t>(ep->ContextRecord->Rip), static_cast<uint64_t>(ep->ExceptionRecord->ExceptionInformation[1]),
                 static_cast<uint32_t>(c->lr), c->r1.u32, c->r3.u32, c->r4.u32, c->r30.u32, c->r31.u32, c->last_indirect_target);
    // Objects behind r31 and the pointers in it: 0x80 bytes each, with the write kinds of their 16-byte blocks
    // (pagemap build) and whether the snapshot held the page
    const auto in_game = [](uint32_t a) { return (a >= 0x82726000u && a < 0x82DB8000u) || (a >= 0xA7290000u && a < 0xABA90000u); };
    const auto dump = [&](const char* what, uint32_t a) {
      if (!in_game(a) && !(a >= 0x30000000u && a < 0x40000000u)) return;
      std::string s;
      for (uint32_t k = 0; k < 0x80; k += 4) s += fmt::format(" {:08X}", T6Rd32(a + k));
      const auto fk = [](uint32_t x) -> int {
        if (x - 0xA7290000u < 0x4800000u) return t6_fine_heap[(x - 0xA7290000u) >> 4];
        if (x - 0x82726000u < 0x692000u) return t6_fine_data[(x - 0x82726000u) >> 4];
        return -1;
      };
      const int64_t si = ShadowIndex(a >> 12);
      REXLOG_ERROR("T6 crash: {} {:08X} page kind {:X} fine {:X} saved {}:{}", what, a, t6_pagemap[a >> 12], fk(a),
                   si >= 0 && si < static_cast<int64_t>(snaptest().saved_pages.size()) ? int(snaptest().saved_pages[si]) : -1, s);
    };
    dump("r31", c->r31.u32);
    if (in_game(c->r31.u32)) {
      for (uint32_t k = 0; k < 0x80; k += 4) {
        const uint32_t p = T6Rd32(c->r31.u32 + k);
        if (p != c->r31.u32 && (in_game(p) || (p >= 0x30000000u && p < 0x40000000u))) dump(fmt::format("[r31+{:X}]", k).c_str(), p);
      }
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
struct T6CrashInstall {
  T6CrashInstall() { AddVectoredExceptionHandler(0, T6CrashHandler); }
} g_t6_crash_install;
}  // namespace t6np
