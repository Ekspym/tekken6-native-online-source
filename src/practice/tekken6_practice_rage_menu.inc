// Include once, at the END of tekken6_guest_hooks.cpp.
// Experimental native FREE STYLE practice-menu extension for the supplied XEX.
#pragma once
#include "tekken6_quick_select_menu_records.inc"
#include <rex/runtime.h>
#include <rex/memory.h>
#include <cstring>
void RestorePracticeSettings();

namespace practice_rage_menu_detail {
constexpr uint32_t originalCount = 496;
constexpr uint32_t recordSize = 96;
constexpr uint32_t textSize = 40;
constexpr uint32_t totalCount = quick_select_roster::totalCount;
constexpr uint32_t p1Row = 496;
constexpr uint32_t p2Row = 499;
std::atomic<uint32_t> menuRecords{0};
std::mutex installMutex;
uint32_t menuText = 0;

uint32_t read32(const uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void write32(uint8_t* base, uint32_t a, uint32_t v) {
  uint8_t* p = base + a;
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
void log(const char* message, uint32_t a = 0, uint32_t b = 0) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  static std::FILE* file = std::fopen("rage-menu-install.txt", "w");
  if (!file) return;
  std::fprintf(file, "%s first=%08X second=%08X\n", message,
               static_cast<unsigned>(a), static_cast<unsigned>(b));
  std::fflush(file);
}
}

void InstallPracticeRageMenu(PPCRegister& records, PPCRegister& texts) {
  using namespace practice_rage_menu_detail;
  std::lock_guard<std::mutex> lock(installMutex);
  auto* runtime = rex::Runtime::instance();
  if (!runtime || !runtime->memory()) return;
  uint8_t* base = runtime->virtual_membase();
  if (!base) return;

  uint32_t installed = menuRecords.load(std::memory_order_acquire);
  if (installed) {
    records.u64 = installed;
    texts.u64 = menuText;
    return;
  }
  // Fail closed if this is not the exact original table layout.
  if (records.u32 != 0x827B36C0u || texts.u32 != 0x827AE938u ||
      read32(base, records.u32 + 296 * recordSize + 8) != 14 ||
      read32(base, records.u32 + 332 * recordSize + 24) != 335 ||
      read32(base, records.u32 + 335 * recordSize + 20) != 332 ||
      read32(base, records.u32 + 297 * recordSize + 8) != 2 ||
      read32(base, records.u32 + 299 * recordSize + 24) != 0 ||
      read32(base, records.u32 + 350 * recordSize + 8) != 2 ||
      read32(base, records.u32 + 352 * recordSize + 24) != 0 ||
      read32(base, records.u32 + 382 * recordSize + 8) != 2 ||
      read32(base, records.u32 + 384 * recordSize + 24) != 0 ||
      read32(base, records.u32 + 495 * recordSize) != 495) {
    log("layout-mismatch", records.u32, texts.u32);
    return;
  }

  // Diagnostic: capture the native hierarchy before adding custom records.
  // The output contains IDs and links only; guest memory is not modified.
  if (std::FILE* table = std::fopen("quick-select-native-menu-table.txt", "w")) {
    std::fputs("id parent children first previous next\n", table);
    for (uint32_t i = 0; i < originalCount; ++i) {
      const uint32_t r = records.u32 + i * recordSize;
      std::fprintf(table, "%u %u %u %u %u %u\n", i,
                   read32(base, r + 4), read32(base, r + 8),
                   read32(base, r + 16), read32(base, r + 20),
                   read32(base, r + 24));
    }
    std::fclose(table);
  }

  const uint32_t size = totalCount * (recordSize + textSize) + quick_select_roster::stringBudget;
  const uint32_t allocation = runtime->memory()->SystemHeapAlloc(size, 32);
  if (!allocation) { log("allocation-failed"); return; }
  const uint32_t newRecords = allocation;
  const uint32_t newTexts = allocation + totalCount * recordSize;
  uint32_t strings = newTexts + totalCount * textSize;
  std::memset(base + allocation, 0, size);
  std::memcpy(base + newRecords, base + records.u32,
              originalCount * recordSize);
  std::memcpy(base + newTexts, base + texts.u32,
              originalCount * textSize);

  auto text = [&](const char* value) {
    const uint32_t result = strings;
    const size_t length = std::strlen(value) + 1;
    std::memcpy(base + strings, value, length);
    strings += static_cast<uint32_t>(length);
    return result;
  };
  const uint32_t minusOne = text("-1");
  const char* labels[] = {"PLAYER RAGE", "OFF", "ON",
                          "OPPONENT RAGE", "OFF", "ON"};
  for (uint32_t n = 0; n < 6; ++n) {
    const uint32_t id = originalCount + n;
    const uint32_t source = n % 3 == 0 ? 323 : (n % 3 == 1 ? 324 : 325);
    const uint32_t r = newRecords + id * recordSize;
    const uint32_t t = newTexts + id * textSize;
    std::memcpy(base + r, base + newRecords + source * recordSize, recordSize);
    std::memcpy(base + t, base + newTexts + source * textSize, textSize);
    write32(base, r, id);
    write32(base, r + 28, id * 10);
    // Clear transient value, animation and disabled flags for added entries.
    std::memset(base + r + 48, 0, recordSize - 48);
    write32(base, t, text(labels[n]));
    // Negative localization IDs select the game's DebugName text fallback.
    for (uint32_t field = 1; field <= 4; ++field)
      write32(base, t + field * 4, minusOne);
    if (n % 3 == 0) {
      write32(base, r + 4, 296);
      write32(base, r + 16, id + 1);
      write32(base, r + 20, n == 0 ? 332 : p1Row);
      write32(base, r + 24, n == 0 ? p2Row : 335);
    } else {
      const uint32_t parent = id - n % 3;
      write32(base, r + 4, parent);
      write32(base, r + 20, n % 3 == 1 ? 0 : id - 1);
      write32(base, r + 24, n % 3 == 1 ? id + 1 : 0);
    }
  }
  write32(base, newRecords + 296 * recordSize + 8, 16);
  write32(base, newRecords + 332 * recordSize + 24, p1Row);
  write32(base, newRecords + 335 * recordSize + 20, p2Row);
  InstallPracticeQuickSelectRecords(base, newRecords, newTexts, strings);
  menuText = newTexts;
  menuRecords.store(newRecords, std::memory_order_release);
  records.u64 = newRecords;
  texts.u64 = newTexts;
  RestorePracticeSettings();
  log("installed-six-records", newRecords, newTexts);
}

void ApplyPracticeRageMenuThreshold(
    PPCRegister& fighter, PPCRegister& health, PPCRegister& threshold) {
  using namespace practice_rage_menu_detail;
  if (!practiceRageOwner.load(std::memory_order_acquire)) return;
  const uint32_t records = menuRecords.load(std::memory_order_acquire);
  auto* runtime = rex::Runtime::instance();
  if (records && runtime) {
    const uint8_t* base = runtime->virtual_membase();
    if (base) {
      practiceP1Rage.store(read32(base, records + p1Row * recordSize + 80) == 1,
                           std::memory_order_relaxed);
      practiceP2Rage.store(read32(base, records + p2Row * recordSize + 80) == 1,
                           std::memory_order_relaxed);
    }
  }
  ApplyPracticeRageThreshold(fighter, health, threshold);
}

void StylePracticeRageNormal(
    PPCRegister& valueRecord, PPCRegister& color)
{
  using namespace practice_rage_menu_detail;

  const uint32_t records =
      menuRecords.load(std::memory_order_acquire);

  if (!records)
    return;

  const uint32_t p1Normal =
      records + (p1Row + 1) * recordSize;
  const uint32_t p2Normal =
      records + (p2Row + 1) * recordSize;

  if (valueRecord.u32 == p1Normal ||
      valueRecord.u32 == p2Normal)
  {
    // Exact green used by the game's native Off values.
    color.u64 = 0xFF00FF80u;
  }
}
