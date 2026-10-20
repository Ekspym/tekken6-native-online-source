#include "t6_common.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

uint8_t* g_t6_membase = nullptr;
bool (*g_t6_gpu_idle)() = nullptr;
uint64_t (*g_t6_gpu_ptrs)() = nullptr;
uint32_t (*g_t6_sys_alloc)(uint32_t, uint32_t) = nullptr;
void (*g_t6_sys_free)(uint32_t) = nullptr;
extern "C" constinit thread_local unsigned char t6_thread_bit = 0;

int main() {
  std::array<uint8_t, 8> memory{0x12,0x34,0x56,0x78,0xAB,0xCD,0,0};
  g_t6_membase = memory.data();
  if (T6Rd32(0) != 0x12345678u || T6Rd16(4) != 0xABCDu) return 1;

  T6SetParam("T6_SMOKE", "projection");
  if (!T6Env("T6_SMOKE") || std::strcmp(T6Env("T6_SMOKE"), "projection")) return 2;

  const std::string path = "/tmp/t6-common-linux-smoke.txt";
  T6WriteFileAtomic(path, "linux-native-common-pass\n");
  FILE* file = T6Open(path, "r");
  if (!file) return 3;
  char line[64]{};
  std::fgets(line, sizeof(line), file);
  std::fclose(file);
  if (std::string(line) != "linux-native-common-pass\n") return 4;

  const double start = T6NowMs();
  T6PlatformSleepMs(2);
  if (T6NowMs() < start) return 5;

  std::printf("T6_COMMON_LINUX_SMOKE=PASS rd32=%08x rd16=%04x\n", T6Rd32(0), T6Rd16(4));
  return 0;
}
