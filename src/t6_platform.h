#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>

using DWORD = uint32_t;
using ULONGLONG = uint64_t;
using BOOL = int;
using HANDLE = void*;
using LONG = long;
constexpr BOOL FALSE = 0;
constexpr BOOL TRUE = 1;
struct LARGE_INTEGER { long long QuadPart; };
struct MEMORY_BASIC_INFORMATION {
  void* BaseAddress = nullptr;
  size_t RegionSize = 0;
  DWORD State = 0;
  DWORD Protect = 0;
  DWORD Type = 0;
};
constexpr DWORD MEM_COMMIT = 0x00001000;
constexpr DWORD PAGE_NOACCESS = 0x00000001;
constexpr DWORD PAGE_EXECUTE = 0x00000010;
constexpr DWORD PAGE_READONLY = 0x00000002;
constexpr DWORD PAGE_READWRITE = 0x00000004;
constexpr DWORD PAGE_WRITECOPY = 0x00000008;
constexpr DWORD PAGE_EXECUTE_READ = 0x00000020;
constexpr DWORD PAGE_EXECUTE_READWRITE = 0x00000040;
constexpr DWORD PAGE_GUARD = 0x00000100;

inline size_t VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, size_t info_size) {
  if (!address || !info || info_size < sizeof(*info)) return 0;
  FILE* maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return 0;
  const uintptr_t wanted = reinterpret_cast<uintptr_t>(address);
  char line[512];
  while (std::fgets(line, sizeof(line), maps)) {
    unsigned long long start = 0, end = 0;
    char perms[5]{};
    if (std::sscanf(line, "%llx-%llx %4s", &start, &end, perms) != 3) continue;
    if (wanted < start || wanted >= end) continue;
    info->BaseAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(start));
    info->RegionSize = static_cast<size_t>(end - start);
    info->State = MEM_COMMIT;
    if (perms[0] != 'r') info->Protect = PAGE_NOACCESS;
    else if (perms[1] == 'w' && perms[2] == 'x') info->Protect = PAGE_EXECUTE_READWRITE;
    else if (perms[1] == 'w') info->Protect = PAGE_READWRITE;
    else if (perms[2] == 'x') info->Protect = PAGE_EXECUTE_READ;
    else info->Protect = PAGE_READONLY;
    std::fclose(maps);
    return sizeof(*info);
  }
  std::fclose(maps);
  return 0;
}
inline DWORD GetCurrentThreadId() { return static_cast<DWORD>(::syscall(SYS_gettid)); }
inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(::getpid()); }
inline void YieldProcessor() { std::this_thread::yield(); }
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) {
  if (!src) { dst[0] = '\0'; return EINVAL; }
  const size_t len = std::strlen(src);
  if (len >= N) { dst[0] = '\0'; return ERANGE; }
  std::memcpy(dst, src, len + 1);
  return 0;
}

inline BOOL CopyFileA(const char* from, const char* to, BOOL fail_if_exists) {
  std::error_code ec;
  const auto options = fail_if_exists ? std::filesystem::copy_options::none
                                      : std::filesystem::copy_options::overwrite_existing;
  const bool copied = std::filesystem::copy_file(from, to, options, ec);
  return copied ? TRUE : FALSE;
}

[[noreturn]] inline void ExitProcess(unsigned code) { std::exit(static_cast<int>(code)); }

inline BOOL VirtualProtect(void* address, size_t size, DWORD new_protect, DWORD* old_protect) {
  if (!address || !size) return 0;
  MEMORY_BASIC_INFORMATION current{};
  if (!VirtualQuery(address, &current, sizeof(current))) return 0;
  if (old_protect) *old_protect = current.Protect;
  int prot = PROT_NONE;
  if (new_protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) prot |= PROT_READ;
  if (new_protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) prot |= PROT_WRITE;
  if (new_protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) prot |= PROT_EXEC;
  const long page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size <= 0) return 0;
  const uintptr_t mask = static_cast<uintptr_t>(page_size - 1);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(address) & ~mask;
  const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + mask) & ~mask;
  return ::mprotect(reinterpret_cast<void*>(begin), end - begin, prot) == 0;
}

inline uint32_t _byteswap_ulong(uint32_t value) { return __builtin_bswap32(value); }
inline uint16_t _byteswap_ushort(uint16_t value) { return __builtin_bswap16(value); }
inline uint64_t _byteswap_uint64(uint64_t value) { return __builtin_bswap64(value); }
inline int __popcnt(unsigned value) { return __builtin_popcount(value); }
inline DWORD GetTickCount() {
  using namespace std::chrono;
  return static_cast<DWORD>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline ULONGLONG GetTickCount64() {
  using namespace std::chrono;
  return static_cast<ULONGLONG>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* value) {
  if (!value) return 0;
  value->QuadPart = 1000000000LL;
  return 1;
}
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* value) {
  if (!value) return 0;
  using namespace std::chrono;
  value->QuadPart = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
  return 1;
}
inline void Sleep(DWORD milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
#endif

#if defined(_WIN32)
using T6ProfileHandle = HANDLE;
#else
using T6ProfileHandle = uintptr_t;
#endif

inline void* T6AlignedAlloc(size_t size, size_t alignment) {
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  void* p = nullptr;
  return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
#endif
}

inline void T6AlignedFree(void* p) {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

template <typename Fn>
inline void T6PlatformForEachReadableRange(uint8_t* base, uint32_t lo, uint32_t hi, const Fn& fn) {
#if defined(_WIN32)
  uint64_t a = lo;
  while (a < hi) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(base + a, &mbi, sizeof(mbi))) break;
    const uint64_t region_end = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(mbi.BaseAddress) - base) +
                                mbi.RegionSize;
    const uint64_t end = std::min<uint64_t>(region_end, hi);
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_READONLY))) {
      fn(static_cast<uint32_t>(a), static_cast<uint32_t>(end - a));
    }
    a = end;
  }
#else
  FILE* maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return;
  const uintptr_t wanted_lo = reinterpret_cast<uintptr_t>(base) + lo;
  const uintptr_t wanted_hi = reinterpret_cast<uintptr_t>(base) + hi;
  char line[512];
  while (std::fgets(line, sizeof(line), maps)) {
    unsigned long long start = 0, end = 0;
    char perms[5]{};
    if (std::sscanf(line, "%llx-%llx %4s", &start, &end, perms) != 3 || perms[0] != 'r') continue;
    const uintptr_t rlo = std::max<uintptr_t>(wanted_lo, static_cast<uintptr_t>(start));
    const uintptr_t rhi = std::min<uintptr_t>(wanted_hi, static_cast<uintptr_t>(end));
    if (rlo < rhi) {
      fn(static_cast<uint32_t>(rlo - reinterpret_cast<uintptr_t>(base)),
         static_cast<uint32_t>(rhi - rlo));
    }
  }
  std::fclose(maps);
#endif
}

inline uint32_t T6ByteSwap32(uint32_t value) {
#if defined(_MSC_VER)
  return _byteswap_ulong(value);
#else
  return __builtin_bswap32(value);
#endif
}

inline uint16_t T6ByteSwap16(uint16_t value) {
#if defined(_MSC_VER)
  return _byteswap_ushort(value);
#else
  return __builtin_bswap16(value);
#endif
}

inline FILE* T6PlatformOpen(const char* path, const char* mode) {
#if defined(_WIN32)
  FILE* file = nullptr;
  return fopen_s(&file, path, mode) == 0 ? file : nullptr;
#else
  return std::fopen(path, mode);
#endif
}

inline double T6PlatformNowMs() {
#if defined(_WIN32)
  static const double ms_per_tick = [] {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return 1000.0 / static_cast<double>(frequency.QuadPart);
  }();
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return static_cast<double>(counter.QuadPart) * ms_per_tick;
#else
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
#endif
}

inline void T6PlatformSleepMs(uint32_t milliseconds) {
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

inline bool T6PlatformReplaceFile(const char* from, const char* to) {
#if defined(_WIN32)
  return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return std::rename(from, to) == 0;
#endif
}

inline void* T6PlatformSymbol(const char* windows_module, const char* linux_module,
                              const char* symbol) {
#if defined(_WIN32)
  const HMODULE module = GetModuleHandleA(windows_module);
  return module ? reinterpret_cast<void*>(GetProcAddress(module, symbol)) : nullptr;
#else
  void* address = dlsym(RTLD_DEFAULT, symbol);
  if (address) return address;
#ifdef RTLD_NOLOAD
  if (linux_module && *linux_module) {
    if (void* module = dlopen(linux_module, RTLD_LAZY | RTLD_NOLOAD)) {
      address = dlsym(module, symbol);
      dlclose(module);
    }
  }
#else
  (void)linux_module;
#endif
  (void)windows_module;
  return address;
#endif
}
