#pragma once
// Helpers shared by the Tekken 6 sources (t6_netplay.cpp, t6_status.cpp, t6_watch.cpp): guest memory reads, the
// runtime's exported functions, game state fields used in several places, small parsers and the status file writer.
// (Code review 2026-10-18: the same lookups / reads / parsers were copied into dozens of places.)

#include <atomic>
#include <climits>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "t6_platform.h"

extern uint8_t* g_t6_membase;  // t6_netplay.cpp: guest memory base
extern bool (*g_t6_gpu_idle)();  // t6_netplay.cpp, set by Tekken6App: the GPU executed all submitted commands
extern uint64_t (*g_t6_gpu_ptrs)();  // t6_netplay.cpp, set by Tekken6App: GPU ring read << 32 | write (diagnostics)
extern uint32_t (*g_t6_sys_alloc)(uint32_t size, uint32_t align);  // t6_netplay.cpp, set by Tekken6App: ReXGlue system heap
extern void (*g_t6_sys_free)(uint32_t addr);
extern "C" constinit thread_local unsigned char t6_thread_bit;  // t6_netplay.cpp: 0 on the main (game) thread, 32 elsewhere

// big endian guest word / half word at a guest address
inline uint32_t T6Rd32(uint32_t a) { return T6ByteSwap32(*reinterpret_cast<const uint32_t*>(g_t6_membase + a)); }
inline uint16_t T6Rd16(uint32_t a) { return T6ByteSwap16(*reinterpret_cast<const uint16_t*>(g_t6_membase + a)); }

// The process environment is read only after the start (getenv is not safe against a concurrent write of it). The
// launcher's 'start' line gives match parameters (characters, stage, names ...) at run time: they are kept here and
// take the place of the environment value of the same name. An empty value = not set (as _putenv_s(name, "") did).
struct T6ParamStore {
  std::mutex m;
  std::unordered_map<std::string, const char*> values;  // name -> value (nullptr = not set)
  std::deque<std::string> kept;                          // every value ever set (a returned pointer stays valid)
};
inline T6ParamStore& T6Params() {
  static T6ParamStore s;
  return s;
}
inline void T6SetParam(const char* name, const std::string& value) {
  T6ParamStore& s = T6Params();
  std::lock_guard lock(s.m);
  if (value.empty()) {
    s.values[name] = nullptr;
    return;
  }
  s.kept.push_back(value);
  s.values[name] = s.kept.back().c_str();
}
// A start parameter, else the environment variable (nullptr = neither)
inline const char* T6Env(const char* name) {
  {
    T6ParamStore& s = T6Params();
    std::lock_guard lock(s.m);
    if (const auto it = s.values.find(name); it != s.values.end()) return it->second;
  }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  return std::getenv(name);  // (the one read of the environment: nothing writes it after the start)
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}
// fopen_s: the file or nullptr
inline FILE* T6Open(const std::string& path, const char* mode) {
  return T6PlatformOpen(path.c_str(), mode);
}

// a guest byte read as a signed 8-bit value
inline int T6Signed8(uint8_t b) { return b < 0x80 ? b : b - 256; }

// Destructors that run logging / bookkeeping must not throw: what they catch is counted here (shown with the stats)
inline std::atomic<uint64_t>& T6SwallowedExceptions() {
  static std::atomic<uint64_t> n{0};
  return n;
}

// Text to a number with a check (atoi / atof report no error): the fallback when the text is not a number
inline int T6ToInt(const char* s, int fallback = 0) {
  if (!s) return fallback;
  char* e = nullptr;
  const long v = std::strtol(s, &e, 10);
  return e == s ? fallback : static_cast<int>(v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : v);
}
inline int T6ToInt(const std::string& s, int fallback = 0) { return T6ToInt(s.c_str(), fallback); }
inline double T6ToDouble(const char* s, double fallback = 0.0) {
  if (!s) return fallback;
  char* e = nullptr;
  const double v = std::strtod(s, &e);
  return e == s ? fallback : v;
}

// A log line of a repeated event: its first 'first' times, then every 'every'-th (counts the event)
template <class Counter>
bool T6LogSample(Counter& n, uint64_t first, uint64_t every) {
  const uint64_t k = n++;
  return k < first || (k + 1) % every == 0;
}

// Options from the environment: a switch that is on unless set to 0, a value equal to one character, numbers
inline bool T6EnvDefaultOn(const char* name) {
  const char* v = T6Env(name);
  return !v || *v != '0';
}
inline bool T6EnvIs(const char* name, char value) {
  const char* v = T6Env(name);
  return v && *v == value;
}
inline int T6EnvInt(const char* name, int fallback) {
  const char* v = T6Env(name);
  return v ? T6ToInt(v) : fallback;
}
inline double T6EnvDouble(const char* name, double fallback) {
  const char* v = T6Env(name);
  return v ? T6ToDouble(v) : fallback;
}
inline uint32_t T6EnvU32(const char* name, uint32_t fallback, int base = 0) {
  const char* v = T6Env(name);
  return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, base)) : fallback;
}

// milliseconds of the performance counter (the frequency read once)
inline double T6NowMs() { return T6PlatformNowMs(); }

// Game memory: the XEX data / bss and the game heaps (what the snapshots and the state hashes cover)
constexpr uint32_t kT6DataLo = 0x82726000u, kT6DataHi = 0x82DB0000u, kT6HeapLo = 0xA7290000u, kT6HeapHi = 0xABA90000u;
constexpr std::pair<uint32_t, uint32_t> kT6GameRanges[] = {{kT6DataLo, kT6DataHi}, {kT6HeapLo, kT6HeapHi}};
// [a, a + n) lies in the XEX data / bss or in the game heaps
inline bool T6InGameRange(uint32_t a, uint32_t n) {
  return (a >= kT6DataLo && a + n <= kT6DataHi) || (a >= kT6HeapLo && a + n <= kT6HeapHi);
}
// a word aligned pointer into the game heaps (task tree nodes)
inline bool T6HeapPtr(uint32_t a) { return a >= kT6HeapLo && a < kT6HeapHi && !(a & 3); }

// Game mode state (0x8292E660, +0xC current mode; modes in t6_status.cpp)
constexpr uint32_t kT6ModeStruct = 0x8292E660u;
inline uint32_t T6GameMode() { return T6Rd32(kT6ModeStruct + 0xC); }
// the end of a match: win poses (7 / 8 / 9) and the menus after it (11, 12, 25-28)
inline bool T6AfterMatchMode(uint32_t gm) { return gm == 7 || gm == 8 || gm == 9 || gm == 11 || gm == 12 || (gm >= 25 && gm <= 28); }

// Fight player objects: *(0x828DCE88) / *(0x828DCE8C), 0xC40 bytes, in the XEX data or the game heaps
constexpr uint32_t kT6PlayerPtrs = 0x828DCE88u;
constexpr uint32_t kT6PlayerSize = 0xC40u;
inline uint32_t T6PlayerPtr(uint32_t side) { return T6Rd32(kT6PlayerPtrs + 4 * side); }
inline bool T6PlayerObjectOk(uint32_t pl) {
  return (pl >= 0x82726000u && pl + kT6PlayerSize <= 0x82DB0000u) || (pl >= 0xA7290000u && pl + kT6PlayerSize <= 0xABA90000u);
}

// Mokujin = character codes 38 / 39 (launcher kCharCodes index 19, P1 / P2 costume) in T6_CHAR_P1 / T6_CHAR_P2
inline bool T6MokuEnv(const char* name) {
  const char* v = T6Env(name);
  return v && (T6ToInt(v) == 38 || T6ToInt(v) == 39);
}
inline bool T6MokuMatch() { return T6MokuEnv("T6_CHAR_P1") || T6MokuEnv("T6_CHAR_P2"); }

// "<hex wButtons>:<frames>,..." (test / menu input sequences); frames default 1
inline std::vector<std::pair<uint16_t, uint32_t>> T6ParseSeq(const char* q, uint32_t* total = nullptr) {
  std::vector<std::pair<uint16_t, uint32_t>> out;
  while (q && *q) {
    char* e = nullptr;
    const uint16_t b = static_cast<uint16_t>(std::strtoul(q, &e, 16));
    const uint32_t n = *e == ':' ? std::strtoul(e + 1, &e, 10) : 1;
    out.emplace_back(b, n);
    if (total) *total += n;
    if (*e != ',') break;
    q = e + 1;
  }
  return out;
}
// the buttons a sequence holds at step 'at' (0 after its end)
inline uint16_t T6SeqAt(const std::vector<std::pair<uint16_t, uint32_t>>& seq, uint32_t at) {
  for (const auto& [b, n] : seq) {
    if (at < n) return b;
    at -= n;
  }
  return 0;
}

// "<key><sep><value>,..." (T6_PAD_MAP "A=4000", T6_KEYMAP "41:1000"): fn(key, value) for every item with the separator
template <class Fn>
void T6ForEachPair(const char* list, char sep, const Fn& fn) {
  if (!list) return;
  const std::string all(list);
  for (size_t at = 0; at < all.size();) {
    size_t end = all.find(',', at);
    if (end == std::string::npos) end = all.size();
    const std::string item = all.substr(at, end - at);
    if (const size_t k = item.find(sep); k != std::string::npos) fn(item.substr(0, k), item.substr(k + 1));
    at = end + 1;
  }
}

// A diagnostics file in T6_NET_DUMP_DIR (default: the working directory)
inline std::string T6DumpPath(const std::string& name) {
  const char* d = T6Env("T6_NET_DUMP_DIR");
  return std::string(d && *d ? d : ".") + "/" + name;
}

// A player name for the HUD: at most 15 printable ASCII characters (others '?'), zero padded to 16 bytes
inline void T6PlateName(const char* name, char out[16]) {
  for (int k = 0; k < 16; k++) out[k] = 0;
  for (size_t k = 0; k < 15 && name && name[k]; k++) {
    const unsigned char c = static_cast<unsigned char>(name[k]);
    out[k] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
  }
}

// A file the launcher reads, replaced whole (written to <path>.tmp, then moved): it never sees half a file
inline void T6WriteFileAtomic(const std::string& path, const std::string& text) {
  const std::string tmp = path + ".tmp";
  if (FILE* f = T6Open(tmp, "w")) {
    std::fputs(text.c_str(), f);
    std::fclose(f);
    T6PlatformReplaceFile(tmp.c_str(), path.c_str());
  }
}

// Functions rexruntime.dll exports for this game (resolved once; nullptr = an older runtime without it)
struct T6RexRuntime {
  int (*waiters)(uint32_t handle);                        // threads waiting on a kernel object
  int (*semaphore_count)(uint32_t handle);                // released, not yet taken count of a semaphore
  void* (*last_waiter_context)(uint32_t handle);          // guest context of the last thread that waited on it
  void* (*current_guest_context)();                       // guest context of the calling thread
  void (*global_lock)(int take);                          // the runtime's global critical region
  int (*hold_object)(uint32_t handle, int take, int ms);  // take / give back a guest lock from the host
  uint32_t (*live_guest_objects)(uint32_t* out, uint32_t max);
  long long (*interrupt_info)(uint32_t* lr);
  void* (*thread_by_kthread)(uint32_t kthread, char* name, uint32_t name_size);
  uint64_t (*wait_entries)(uint32_t handle);
  int (*audio_queued)();
  void (*audio_fast)(int on);
  void (*audio_lockstep)(int on);
  int (*audio_advance)(uint32_t frames, int timeout_ms);
  void (*audio_delay)(int frames);
  void* (*audio_callback_context)();
  int (*audio_worker_callback)(uint64_t* number);
  unsigned (*audio_xma_errors)();
  void (*xma_stats)(uint64_t* ns, uint64_t* count);
  void (*sem_stats)(uint64_t* count, uint64_t* ticks);
  uint32_t (*swap_count)();  // (in rexgpu-xenos.dll, older runtimes: rexruntime.dll)
};
inline const T6RexRuntime& T6Rex() {
  static const T6RexRuntime r = [] {
    T6RexRuntime x{};
    const auto get = [](auto& fn, const char* name) {
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(T6PlatformSymbol(
          "rexruntime.dll", "librexruntime.so", name));
    };
    get(x.waiters, "rex_waiters");
    get(x.semaphore_count, "rex_semaphore_count");
    get(x.last_waiter_context, "rex_last_waiter_context");
    get(x.current_guest_context, "rex_current_guest_context");
    get(x.global_lock, "rex_global_lock");
    get(x.hold_object, "rex_hold_object");
    get(x.live_guest_objects, "rex_live_guest_objects");
    get(x.interrupt_info, "rex_interrupt_info");
    get(x.thread_by_kthread, "rex_thread_by_kthread");
    get(x.wait_entries, "rex_wait_entries");
    get(x.audio_queued, "rex_audio_queued");
    get(x.audio_fast, "rex_audio_fast");
    get(x.audio_lockstep, "rex_audio_lockstep");
    get(x.audio_advance, "rex_audio_advance");
    get(x.audio_delay, "rex_audio_delay");
    get(x.audio_callback_context, "rex_audio_callback_context");
    get(x.audio_worker_callback, "rex_audio_worker_callback");
    get(x.audio_xma_errors, "rex_audio_xma_errors");
    get(x.xma_stats, "rex_xma_stats");
    get(x.sem_stats, "rex_sem_stats");
    x.swap_count = reinterpret_cast<uint32_t (*)()>(
        T6PlatformSymbol("rexgpu-xenos.dll", "librexgpu-xenos.so", "rex_swap_count"));
    if (!x.swap_count) get(x.swap_count, "rex_swap_count");
    return x;
  }();
  return r;
}
