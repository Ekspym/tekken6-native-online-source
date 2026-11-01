// Passive diagnostics for the supplied XEX's native stage content bank.
#pragma once
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace stage_load_trace_detail {
std::mutex mutex;
bool span(uint32_t address, uint32_t length) {
  return length <= 0x3E000000u && address >= 0x82000000u &&
         address <= 0xC0000000u - length && !(address & 3);
}
uint32_t read(const uint8_t* base, uint32_t address) {
  const uint8_t* p = base + address;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
         uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
void record(const char* event, uint32_t bank, uint32_t descriptor) {
  std::lock_guard<std::mutex> lock(mutex);
  auto* runtime = rex::Runtime::instance();
  if (!runtime) return;
  const uint8_t* base = runtime->virtual_membase();
  if (!base) return;
  const bool request = std::strcmp(event, "bank-request") == 0;
  // Constructor reads descriptor+20; the loader reads bank+212.
  const uint32_t source = request ? descriptor : bank;
  const uint32_t length = request ? 24u : 216u;
  const bool valid = span(source, length);
  const uint32_t key = valid ? read(base, source + (request ? 20 : 212)) : 0xFFFFFFFFu;
  static unsigned sequence = 0;
  static bool havePrevious = false;
  static bool previousRequest = false;
  static uint32_t previousBank = 0, previousDescriptor = 0, previousKey = 0;
  if (sequence >= 512 || (havePrevious && previousRequest == request &&
      previousBank == bank && previousDescriptor == descriptor && previousKey == key)) return;
  static std::FILE* file = std::fopen("stage-load-trace.txt", "w");
  if (!file) return;
  if (!sequence) std::fputs("TEKKEN6 STAGE LOAD TRACE v1\n", file);
  havePrevious = true; previousRequest = request; previousBank = bank;
  previousDescriptor = descriptor; previousKey = key;
  std::fprintf(file, "EVENT %u %s bank=%08X descriptor=%08X stage-key=%u hex=%08X valid=%u\n",
               ++sequence, event, bank, descriptor, key, key, unsigned(valid));
  if (request && valid) {
    std::fputs("  descriptor[0..20]:", file);
    for (uint32_t offset = 0; offset < 24; offset += 4)
      std::fprintf(file, " %08X", read(base, descriptor + offset));
    std::fputc('\n', file);
  }
  if (sequence == 512) std::fputs("TRACE LIMIT REACHED\n", file);
  std::fflush(file);
}
}

void TraceStageBankRequest(PPCRegister& bank, PPCRegister& descriptor) {
  stage_load_trace_detail::record("bank-request", bank.u32, descriptor.u32);
}
void TraceStageBankLoad(PPCRegister& bank) {
  stage_load_trace_detail::record("bank-load", bank.u32, 0);
}
