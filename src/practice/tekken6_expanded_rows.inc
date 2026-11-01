// OWNER-SCOPED MENU ROWS FIX
// Include ONCE at the end of tekken6_guest_hooks.cpp, after the Rage menu include.
#pragma once
#include "tekken6_expanded_rows.h"
#include <rex/runtime.h>
#include <rex/memory.h>
#include <map>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>

extern "C" void sub_8236AF50(PPCContext&, uint8_t*);
extern "C" void sub_8236AFE8(PPCContext&, uint8_t*);
extern "C" void sub_82369EC0(PPCContext&, uint8_t*);

namespace t6_expanded_rows {
constexpr uint32_t offsets[3] = {2996, 18676, 34368};
constexpr uint32_t strides[3] = {1120, 1120, 320};
constexpr uint32_t capacity = 18, extraCount = capacity - 14;
constexpr uint32_t extraOffsets[3] = {0, extraCount * 1120, extraCount * 2240};
constexpr uint32_t slotMapOffset = extraCount * (2240 + 320);
constexpr uint32_t allocationSize = slotMapOffset + 32;
struct Storage { uint32_t allocation; };
std::map<uint32_t, Storage> windows;
std::mutex mutex;
std::atomic<uint64_t> generation{0};
void log(const char* event, uint32_t owner) {
  static std::mutex logMutex;
  std::lock_guard<std::mutex> lock(logMutex);
  static std::FILE* f = std::fopen("menu-rows-debug.txt", "w");
  if (f) { std::fprintf(f, "%s owner=%08X\n", event, unsigned(owner)); std::fflush(f); }
}
void store32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24); base[a+1] = uint8_t(v >> 16);
  base[a+2] = uint8_t(v >> 8); base[a+3] = uint8_t(v);
}
uint32_t row(uint32_t owner, const Storage& s, uint32_t bank, uint32_t index) {
  return index < 14 ? owner + offsets[bank] + index * strides[bank]
                    : s.allocation + extraOffsets[bank] + (index - 14) * strides[bank];
}
bool locate(uint32_t address, uint32_t expectedOwner, uint32_t& owner, Storage& storage,
            uint32_t& bank, uint32_t& index) {
  for (const auto& entry : windows) {
    if (entry.first != expectedOwner) continue;
    for (uint32_t k = 0; k < 3; ++k) {
      const uint32_t start = entry.first + offsets[k];
      const uint32_t delta = address - start;
      if (delta < 14 * strides[k] && delta % strides[k] == 0) {
        owner = entry.first; storage = entry.second; bank = k; index = delta / strides[k]; return true;
      }
      const uint32_t extra = entry.second.allocation + extraOffsets[k];
      const uint32_t extraDelta = address - extra;
      if (extraDelta < extraCount * strides[k] && extraDelta % strides[k] == 0) {
        owner = entry.first; storage = entry.second; bank = k; index = 14 + extraDelta / strides[k]; return true;
      }
    }
  }
  return false;
}
}
uint64_t T6RowsGeneration(){return t6_expanded_rows::generation.load(std::memory_order_acquire);}
uint32_t T6RowsCapacity(uint32_t owner) {
  std::lock_guard<std::mutex> lock(t6_expanded_rows::mutex);
  return t6_expanded_rows::windows.count(owner) ? t6_expanded_rows::capacity : 14u;
}
// Keep the extra slots out of owner+2988, which is a live native pointer.
uint32_t T6RowsSlotMap(uint32_t address, uint32_t expectedOwner) {
  using namespace t6_expanded_rows;
  std::lock_guard<std::mutex> lock(mutex);
  const auto it = windows.find(expectedOwner);
  if (it == windows.end()) return address;
  const uint32_t index = address - expectedOwner - 2972;
  return index < capacity ? it->second.allocation + slotMapOffset + index : address;
}
uint32_t T6RowsMap(uint32_t address, uint32_t bank, uint32_t expectedOwner) {
  using namespace t6_expanded_rows;
  if (bank > 2) return address;
  std::lock_guard<std::mutex> lock(mutex);
  for (const auto& entry : windows) {
    if (entry.first != expectedOwner) continue;
    const uint32_t delta = address - entry.first - offsets[bank];
    if (delta < capacity * strides[bank])
      return row(entry.first, entry.second, bank, delta / strides[bank]) + delta % strides[bank];
  }
  return address;
}
uint32_t T6RowsAdvance(uint32_t address, int32_t step, uint32_t expectedOwner) {
  using namespace t6_expanded_rows;
  std::lock_guard<std::mutex> lock(mutex);
  uint32_t owner{}, bank{}, index{}; Storage s{};
  if (locate(address, expectedOwner, owner, s, bank, index) && step == int32_t(strides[bank]) && index + 1 < capacity)
    return row(owner, s, bank, index + 1);
  return address + uint32_t(step);
}
uint32_t T6RowsRelated(uint32_t address, uint32_t targetBank, uint32_t expectedOwner) {
  using namespace t6_expanded_rows;
  std::lock_guard<std::mutex> lock(mutex);
  uint32_t owner{}, bank{}, index{}; Storage s{};
  if (targetBank < 3 && locate(address, expectedOwner, owner, s, bank, index))
    return row(owner, s, targetBank, index);
  // This helper is emitted only for +/-15680 transitions between label banks.
  return targetBank == 0 ? address - 15680u : address + 15680u;
}
void T6RowsInitialize(PPCContext& ctx, uint8_t* base, uint32_t owner) {
  using namespace t6_expanded_rows;
  auto* runtime = rex::Runtime::instance();
  if (!runtime || !runtime->memory() || !owner) return;
  { std::lock_guard<std::mutex> lock(mutex); if (windows.count(owner)) return; }
  const uint32_t allocation = runtime->memory()->SystemHeapAlloc(allocationSize, 32);
  if (!allocation) { log("allocation-failed-capacity-14", owner); return; }
  std::memset(base + allocation, 0, allocationSize);
  std::memset(base + allocation + slotMapOffset, 255, 32);
  // Run the original guest constructors with the current guest thread/TLS/stack.
  // Restore all caller registers after each constructor, as an ordinary callee would.
  const PPCContext saved = ctx;
  for (uint32_t bank = 0; bank < 3; ++bank) {
    for (uint32_t index = 0; index < extraCount; ++index) {
      ctx = saved; ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
      ctx.r3.u64 = allocation + extraOffsets[bank] + index * strides[bank];
      if (bank == 2) sub_8236AFE8(ctx, base); else sub_8236AF50(ctx, base);
    }
  }
  ctx = saved; ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  // Preserve native slot initialization; all adapted accesses use external storage.
  std::memcpy(base + allocation + slotMapOffset, base + owner + 2972, 14);
  { std::lock_guard<std::mutex> lock(mutex); windows.emplace(owner, Storage{allocation}); generation.fetch_add(1,std::memory_order_release); }
  log("capacity-18", owner);
}
void T6RowsDestroy(PPCContext& ctx, uint8_t* base, uint32_t owner) {
  using namespace t6_expanded_rows;
  uint32_t allocation = 0;
  { std::lock_guard<std::mutex> lock(mutex);
    const auto it = windows.find(owner); if (it == windows.end()) return;
    allocation = it->second.allocation; windows.erase(it); generation.fetch_add(1,std::memory_order_release);
  }
  const PPCContext saved = ctx;
  for (int bank = 2; bank >= 0; --bank) {
    for (int index = int(extraCount) - 1; index >= 0; --index) {
      const uint32_t address = allocation + extraOffsets[bank] + uint32_t(index) * strides[bank];
      store32(base, address, bank == 2 ? 0x8207B54Cu : 0x8207B48Cu);
      ctx = saved; ctx.fpscr.restoreGuestBits(saved.fpscr.csr); ctx.r3.u64 = address; sub_82369EC0(ctx, base);
    }
  }
  ctx = saved; ctx.fpscr.restoreGuestBits(saved.fpscr.csr);
  auto* runtime = rex::Runtime::instance();
  if (runtime && runtime->memory()) runtime->memory()->SystemHeapFree(allocation);
  log("released-extra-rows", owner);
}
