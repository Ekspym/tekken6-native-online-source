/* Linux compatibility surface for the bundled GGPO fork. */
#ifndef _GGPO_LINUX_H_
#define _GGPO_LINUX_H_

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <thread>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <strings.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "types.h"

using SOCKET = int;
using DWORD = uint32;
using HANDLE = int;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif
#ifndef INFINITE
#define INFINITE (-1)
#endif
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

inline int closesocket(SOCKET s) { return ::close(s); }
inline int ioctlsocket(SOCKET s, unsigned long request, void* argp) {
   return ::ioctl(s, request, argp);
}
inline int WSAGetLastError() { return errno; }
inline void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline int _stricmp(const char* a, const char* b) { return ::strcasecmp(a, b); }
inline void OutputDebugStringA(const char* msg) {
   if (msg) std::fputs(msg, stderr);
}
inline void DebugBreak() { ::raise(SIGTRAP); }
inline int CreateDirectoryA(const char* path, void*) {
   const int r = ::mkdir(path, 0755);
   return r == 0 || errno == EEXIST;
}

template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) {
   if (!src) {
      if constexpr (N) dst[0] = '\0';
      return EINVAL;
   }
   const size_t len = std::strlen(src);
   if (len >= N) {
      if constexpr (N) dst[0] = '\0';
      return ERANGE;
   }
   std::memcpy(dst, src, len + 1);
   return 0;
}

inline int sprintf_s(char* dst, size_t size, const char* fmt, ...) {
   va_list args;
   va_start(args, fmt);
   const int result = std::vsnprintf(dst, size, fmt, args);
   va_end(args);
   return result;
}

inline int vsprintf_s(char* dst, size_t size, const char* fmt, va_list args) {
   return std::vsnprintf(dst, size, fmt, args);
}
inline int fopen_s(FILE** out, const char* path, const char* mode) {
   if (!out) return EINVAL;
   *out = std::fopen(path, mode);
   return *out ? 0 : errno;
}
inline int strncat_s(char* dst, size_t dst_size, const char* src, size_t count) {
   if (!dst || !src || !dst_size) return EINVAL;
   const size_t used = std::strlen(dst);
   if (used >= dst_size) return ERANGE;
   const size_t room = dst_size - used - 1;
   const size_t take = std::min(room, std::min(count, std::strlen(src)));
   std::memcpy(dst + used, src, take);
   dst[used + take] = '\0';
   return take < std::min(count, std::strlen(src)) ? ERANGE : 0;
}

class Platform {
public:
   using ProcessID = pid_t;
   static ProcessID GetProcessID() { return ::getpid(); }
   static void AssertFailed(char* msg) {
      OutputDebugStringA(msg);
      FILE* f = std::fopen("ggpo_assert.txt", "a");
      if (f) {
         std::fputs(msg, f);
         std::fputc('\n', f);
         std::fclose(f);
      }
   }
   static uint32 GetCurrentTimeMS() {
      using namespace std::chrono;
      return static_cast<uint32>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
   }
   static int GetConfigInt(const char* name);
   static bool GetConfigBool(const char* name);
};

#endif
