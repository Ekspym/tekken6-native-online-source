// QUICK SELECT PACING V2: native Character Select keeps ordinary pacing.
#pragma once
#include "t6_transition_policy.h"
#include <chrono>
#include <mutex>
#include <string>
#include <cstdlib>
#include <rex/cvar.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace t6_manual_transition {
std::mutex mutex;
T6TransitionPolicy policy;
std::string priorVsync;
bool applied=false;
using AudioFast=void(*)(int);
AudioFast audioFast() {
#if defined(_WIN32)
  static const auto fn=[]()->AudioFast {
    for (const wchar_t* module:{L"rexruntime.dll",L"librexruntime.dll"})
      if (HMODULE h=GetModuleHandleW(module))
        if (auto p=GetProcAddress(h,"rex_audio_fast")) return reinterpret_cast<AudioFast>(p);
    return nullptr;
  }();
  return fn;
#else
  return nullptr;
#endif
}
uint64_t now() { return std::chrono::duration_cast<std::chrono::milliseconds>(
  std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool enabled() {
  const char* value=T6_TRANSITION_ENV("TEKKEN6_FAST_QUICK_SELECT");
  return T6_TRANSITION_ALLOWED() && (!value || *value!='0');
}
void apply(bool want) {
  if (want==applied) return;
  auto fast=audioFast();
  if (want) {
    if (!fast) { policy.stop(); return; } // Older SDK: retain ordinary pacing.
    priorVsync=rex::cvar::GetFlagByName("vsync");
    if (priorVsync.empty() || !rex::cvar::SetFlagByName("vsync","false")) {
      policy.stop(); return;
    }
    fast(1); applied=true;
  } else {
    if (fast) fast(0);
    rex::cvar::SetFlagByName("vsync",priorVsync);
    applied=false;
  }
}
void begin() {
  std::lock_guard<std::mutex> lock(mutex);
  policy.ready(false,now());
  if (enabled()) policy.ready(true,now());
  apply(policy.active);
}
void stop() { std::lock_guard<std::mutex> lock(mutex); policy.stop(); apply(false); }
void tick() {
  std::lock_guard<std::mutex> lock(mutex);
  apply(policy.tick(now(),enabled()));
}
}
void T6ManualTransitionTick(PPCRegister&) { t6_manual_transition::tick(); }
