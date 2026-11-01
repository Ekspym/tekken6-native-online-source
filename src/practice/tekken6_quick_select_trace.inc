// Diagnostic hooks only. No guest registers or guest memory are modified.
#pragma once
#include <rex/runtime.h>
#include <rex/ppc/context.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace quick_select_trace_detail {
std::mutex mutex;
unsigned sequence = 0;
constexpr unsigned maxEvents = 2048;
std::FILE* output() {
  static std::FILE* file = std::fopen("quick-select-trace.txt", "w");
  return file;
}
bool span(uint32_t a, uint32_t size) {
  // The observed SystemHeap objects include A7..., A9..., and AB... addresses.
  // This is a range sanity check, not proof that arbitrary memory is mapped.
  return size <= 0x3E000000u && a >= 0x82000000u &&
         a <= 0xC0000000u - size && !(a & 3);
}
uint32_t read(const uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
         uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
void dump(std::FILE* file, const uint8_t* base, const char* tag,
          uint32_t address, uint32_t size) {
  std::fprintf(file, "%s address=%08X bytes=%u\n", tag, address, size);
  if (!span(address, size)) {
    std::fputs("  skipped: null, unaligned, or outside observed guest range\n", file);
    return;
  }
  for (uint32_t i = 0; i < size; i += 4) {
    if (!(i % 32)) std::fprintf(file, "  +%03X:", i);
    std::fprintf(file, " %08X", read(base, address + i));
    if (i % 32 == 28 || i + 4 == size) std::fputc('\n', file);
  }
}
void snapshot(const char* event, uint32_t a, uint32_t b,
              uint32_t c = 0, uint32_t d = 0) {
  std::lock_guard<std::mutex> lock(mutex);
  static const char* previousEvent = nullptr;
  static uint32_t previous[4] = {};
  if (previousEvent == event && previous[0] == a && previous[1] == b &&
      previous[2] == c && previous[3] == d) return;
  if (sequence >= maxEvents) return;
  auto* runtime = rex::Runtime::instance();
  if (!runtime) return;
  const uint8_t* base = runtime->virtual_membase();
  if (!base) return;
  auto* file = output();
  if (!file) return;
  previousEvent = event;
  previous[0] = a; previous[1] = b; previous[2] = c; previous[3] = d;
  if (!sequence) std::fputs("QUICK SELECT TRACE v2\n", file);
  std::fprintf(file, "\nEVENT %u %s a=%08X b=%08X c=%08X d=%08X\n",
               ++sequence, event, a, b, c, d);
  // This exact registry and generation/flag check are used by 0x8238E3B8.
  constexpr uint32_t registry = 0x82735C70u;
  const uint32_t root = read(base, registry + 16);
  std::fprintf(file, "registry-root=%08X generation=%08X\n", root,
               read(base, registry + 20));
  if (span(root, 1028) &&
      read(base, root + 12) == read(base, registry + 20) &&
      (read(base, root + 60) & read(base, 0x82007560u))) {
    // Named as raw services until their selection fields are verified.
    dump(file, base, "service856", read(base, root + 856), 512);
    dump(file, base, "service908", read(base, root + 908), 352);
  } else {
    std::fputs("registry snapshot skipped: range, generation, or live-flag check failed\n", file);
  }
  if (std::strcmp(event, "menu-dispatch") == 0 && span(a, 4)) {
    const uint32_t table = read(base, a);
    std::fprintf(file, "dispatch-vtable=%08X\n", table);
    // 0x8242E2F8 routes row 346 through slot 1920 and row 347 through 1924.
    // Record the live table target instead of assuming the base-table callback.
    if ((b == 346 || b == 347) && span(table, 1928))
      std::fprintf(file, "dispatch-target=%08X slot=%u\n",
                   read(base, table + (b == 346 ? 1920 : 1924)),
                   b == 346 ? 1920u : 1924u);
  }
  if (event[0] == 'p') {
    dump(file, base, "practice-owner", a, 352);
    dump(file, base, "p1", b, 256);
    dump(file, base, "p2", c, 256);
  }
  if (sequence == maxEvents) std::fputs("TRACE LIMIT REACHED\n", file);
  std::fflush(file);
}
}

// BEGIN DEFENSE COMMAND TRACE V1
void TraceDefenseCommandEvent(const char* kind, uint32_t object, uint32_t id,
                              uint32_t action, uint32_t value) {
  const char* enabled=T6PracticeEnv("TEKKEN6_TRACE_DEFENSE_COMMAND");
  if (!enabled || std::strcmp(enabled,"1") != 0) return;
  using namespace quick_select_trace_detail;
  std::lock_guard<std::mutex> lock(mutex);
  auto* runtime=rex::Runtime::instance();
  if(!runtime) return;
  const uint8_t* base=runtime->virtual_membase(); if(!base) return;
  static std::FILE* f=std::fopen("defense-command-trace.txt","w");
  static unsigned events=0;
  if(!f || events>=1000) return;
  const uint32_t root=read(base,0x82735C80u);
  uint32_t service=0,ctx=0;
  if(span(root,912) && read(base,root+12)==read(base,0x82735C84u) &&
      (read(base,root+60)&read(base,0x82007560u))) {
    service=read(base,root+908);
    if(span(service,320)) {
      const uint32_t candidate=read(base,service+312);
      if(span(candidate,420) && read(base,candidate+12)==read(base,service+316) &&
          (read(base,candidate+60)&read(base,0x82007560u))) ctx=candidate;
    }
  }
  std::fprintf(f,"EVENT %u %s object=%08X id=%u action=%08X value=%08X context=%08X\n",
               ++events,kind,object,id,action,value,ctx);
  // BEGIN DEFENSE COMMAND DETAIL V2
  if(ctx) {
    dump(f,base,"practice-context",ctx,420);
    // These are candidate guest pointers observed in practice-context snapshots.
    const uint32_t offsets[]={0xB8u,0xC4u,0x184u,0x18Cu};
    for(uint32_t offset:offsets) {
      const uint32_t candidate=read(base,ctx+offset);
      // Only inspect aligned addresses in the observed SystemHeap range.
      if(candidate>=0xA0000000u && candidate<=0xBFFFFF00u && !(candidate&3)) {
        char label[40];std::snprintf(label,sizeof(label),"context-pointer-%03X",offset);
        dump(f,base,label,candidate,256);
      }
    }
  }
  if(span(object,4)) {
    const uint32_t vt=read(base,object);
    if(span(vt,576)) std::fprintf(f,"apply-callback-386=%08X\n",read(base,vt+572));
  }
  // END DEFENSE COMMAND DETAIL V2
  if(span(object,4)) {
    uint32_t vt=read(base,object);
    if(span(vt,2000)) dump(f,base,"menu-vtable",vt+1880,120);
  }
  std::fflush(f);
}
// END DEFENSE COMMAND TRACE V1
void TraceQuickSelectMenu(PPCRegister& object, PPCRegister& id,
                          PPCRegister& action, PPCRegister& value) {
  TraceDefenseCommandEvent("menu",object.u32,id.u32,action.u32,value.u32); // DEFENSE COMMAND TRACE CALL V1
  // Record action rows associated with practice menus; avoid per-frame rows.
  const uint32_t n = id.u32;
  if (n == 300 || n == 346 || n == 347 || n == 353 || n == 378 ||
      n == 379 || n == 385 || n == 411 || n == 412)
    quick_select_trace_detail::snapshot("menu-dispatch", object.u32,
                                        n, action.u32, value.u32);
}
void TraceQuickSelectCharacterScreen(PPCRegister& object) {
  quick_select_trace_detail::snapshot("character-select-action", object.u32, 0);
}
void TraceQuickSelectMenuClose(PPCRegister& object) {
  quick_select_trace_detail::snapshot("menu-close", object.u32, 0);
}
void TraceQuickSelectPractice(PPCRegister& owner, PPCRegister& p1,
                              PPCRegister& p2) {
  // This routine also runs when practice settings are reapplied to an owner.
  quick_select_trace_detail::snapshot("practice-setup", owner.u32, p1.u32, p2.u32);
}
void TraceQuickSelectFighterSetting(PPCRegister& fighter, PPCRegister& id,
                                    PPCRegister& other) {
  quick_select_trace_detail::snapshot("fighter-setting-pair", fighter.u32,
                                      id.u32, other.u32);
}
void TraceQuickSelectFighterId(PPCRegister& fighter, PPCRegister& id) {
  quick_select_trace_detail::snapshot("fighter-setting-id", fighter.u32, id.u32);
}
