// Tekken 6 netplay - XAudio2 voices across rollbacks: stale voices, lifetimes, orphans (split out of t6_netplay.cpp, see the overview there)

#include "t6_netplay_internal.h"

// ---- XAudio2 voice wrappers: guard against stale voices after a rollback (an earlier analysis) ----
// The game objects that hold a voice pointer (MMC track +284 -> library object -> voice, CnuSound voice entries) are
// in the snapshot, the voices (0x30xxxxxx heap) are not: a load brings back a pointer to a voice the game destroyed in
// the discarded frames. While the freed block still held the old voice the calls "worked"; once it was reused, the
// wrapper called through a vtable word like 0x3F800000 and the main thread crashed (sub_824EECF0, lr 824EED38) - seen
// whenever the frame cost / allocation order changed (re-simulation skips). Every wrapper is
// { lock 0x827D83A4; voice->vtbl[n](voice, ...); unlock }, voice in r3: in a netplay run a voice whose first word is
// not a vtable in the image is not touched and the call returns XAUDIO2_E_INVALID_CALL.
namespace t6np {
// online and in the practice: the practice tools' position slots load states tens of seconds old (user 2026-10-19:
// the practice crashed in XAudio2 sub_824EED98 on a voice with the first word 0x3F800000 right after a slot load)
bool VoiceGuardOn() {
  static const bool on = net_config().enabled || T6Env("T6_PRACTICE") != nullptr;
  return on;
}
uint64_t g_stale_voice_calls = 0;
bool StaleVoice(PPCContext& ctx, uint8_t* base, uint32_t fn) {
  if (!VoiceGuardOn()) return false;
  const uint32_t v = ctx.r3.u32;
  // only pointers into the voice heap are judged (sub_824EEDE0 gets a parameter block on the stack in r3)
  if (v < 0x30000000u || v >= 0x40000000u) return false;
  uint32_t vt = 0;
  const bool mapped = v >= 0x10000u && (v & 3) == 0 && PageReadable(v >> 12);
  if (mapped) vt = T6Rd32(v);
  // a live voice: vtable in the image's read-only data whose slots are code addresses
  if (mapped && vt >= 0x82000000u && vt < 0x82170000u && (vt & 3) == 0) {
    const uint32_t f0 = T6Rd32(vt);
    const uint32_t f92 = T6Rd32(vt + 92);
    if (f0 >= 0x82170000u && f0 < 0x82724000u && f92 >= 0x82170000u && f92 < 0x82724000u) return false;
  }
  if (T6LogSample(g_stale_voice_calls, 20, 1000)) {
    REXLOG_WARN("T6 net: XAudio2 call {:08X} on a stale voice {:08X} (first word {:08X}, lr {:08X}, frame {}{}) ignored, {} so far", fn, v,
                vt, static_cast<uint32_t>(ctx.lr), g_net_frame, g_net_resim ? ", re-simulated" : "", g_stale_voice_calls);
  }
  ctx.r3.u64 = 0x88960001u;  // XAUDIO2_E_INVALID_CALL
  return true;
}
}  // namespace t6np

// Identity on top of the validity test (the block of a released voice was reused by an object of another class with a
// valid vtable: sv3, frame 5206): voices and MMC library objects released by the game are remembered as dead until an
// object is created at that address again; calls on a dead object do nothing.
//   voice: create sub_824EEFC0 / sub_824EF020 (params r3, out** r4; r3 >= 0 = ok), release sub_824EE840 (vtbl[8],
//          returns the remaining reference count), all other per-voice stubs below;
//   MMC library object (*(mmc+284)): created in sub_82341C88 (out = this+284), release sub_82583C88 (vtbl[8]), stop
//          sub_82583FA0, get voice sub_82583FB0(obj, out*), 3D apply sub_82583C98.
namespace t6np {
std::mutex g_dead_mutex;
std::unordered_map<uint32_t, uint32_t> g_dead;  // address -> frame of the release
uint64_t g_dead_calls = 0;
bool Dead(uint32_t a) {
  std::lock_guard lock(g_dead_mutex);
  return g_dead.count(a) != 0;
}
void SetDead(uint32_t a, bool dead) {
  std::lock_guard lock(g_dead_mutex);
  if (dead) g_dead[a] = g_net_frame; else g_dead.erase(a);
}
bool DeadCall(PPCContext& ctx, uint8_t* base, uint32_t fn, uint32_t result) {
  if (!VoiceGuardOn()) return false;
  const uint32_t v = ctx.r3.u32;
  if (v != 0 && !Dead(v) && !StaleVoice(ctx, base, fn)) return false;
  if (T6LogSample(g_dead_calls, 20, 1000)) {
    REXLOG_WARN("T6 net: sound call {:08X} on a released object {:08X} (lr {:08X}, frame {}{}) ignored, {} so far", fn, v,
                static_cast<uint32_t>(ctx.lr), g_net_frame, g_net_resim ? ", re-simulated" : "", g_dead_calls);
  }
  ctx.r3.u64 = result;
  return true;
}
}  // namespace t6np

// ---- Sound lifetimes (T6_SND_LIFE=<csv file>; user 2026-10-15: hit sounds cut online, never in practice) ----
// One line per voice when its last reference is released: when it was created (frame, game mode, the game -> library
// sound call it came from, the creator's caller, re-simulated or live), how long it lived, the last state the game
// read from it (GetState vtbl[48]: 0x20 = finished playing), the calls of vtbl[36] / vtbl[40] (start / stop) on it
// and the caller of the release. A sound released while its last state was not "finished" ended early = a cut; the
// same columns from practice and online show which path cuts. Off by default (nothing recorded, one flag test).
namespace t6np {
struct SndLife {
  double t0 = 0;
  uint32_t f0 = 0, mode0 = 0, snd_addr = 0, snd_lr = 0, lr = 0;
  bool resim = false;
  uint8_t last_state = 0xFF;
  uint32_t n36 = 0, n40 = 0, lr40 = 0, call40 = 0, call40_lr = 0;  // (call40: the game -> library call of the last stop)
  double t40 = 0;
  bool resim40 = false;
  uint8_t entry[64] = {};  // the CnuSound entry (slot - 4) right after the create
};
bool g_life_orphan = false;  // the release running is the one of an orphan after a rollback
std::mutex g_life_mutex;
std::unordered_map<uint32_t, SndLife> g_life;
FILE* g_life_file = nullptr;
bool LifeOn() {
  static const bool on = [] {
    const char* p = T6Env("T6_SND_LIFE");
    if (!p || !*p) return false;
    g_life_file = T6Open(p, "w");
    if (g_life_file)
      std::fputs("voice,net,create_frame,create_mode,create_resim,sound_call,sound_call_lr,creator_lr,life_ms,last_state,"
                 "starts,stops,stop_lr,stop_call,stop_call_lr,stop_resim,stop_to_release_ms,release_frame,release_mode,release_resim,release_lr,orphan,entry\n", g_life_file);
    return g_life_file != nullptr;
  }();
  return on;
}
void LifeCall(uint32_t fn, uint32_t v, uint32_t lr) {
  if (!LifeOn() || (fn != 0x824EE918u && fn != 0x824EE970u)) return;
  std::lock_guard lock(g_life_mutex);
  const auto it = g_life.find(v);
  if (it == g_life.end()) return;
  if (fn == 0x824EE918u) it->second.n36++;
  else {
    it->second.n40++;
    it->second.lr40 = lr;
    it->second.t40 = T6NowMs();
    it->second.call40 = g_snd_cur_addr;
    it->second.call40_lr = g_snd_cur_lr;
    it->second.resim40 = g_net_resim;
  }
}
}  // namespace t6np
#define T6_VOICE_GUARD(addr)   extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base);   extern "C" void sub_##addr(PPCContext& ctx, uint8_t* base) {     if (DeadCall(ctx, base, 0x##addr##u, 0x88960001u)) return;     LifeCall(0x##addr##u, ctx.r3.u32, static_cast<uint32_t>(ctx.lr));     __imp__sub_##addr(ctx, base);   }
T6_VOICE_GUARD(824EE7D8)
T6_VOICE_GUARD(824EE8A8)
T6_VOICE_GUARD(824EE918)
T6_VOICE_GUARD(824EE970)
T6_VOICE_GUARD(824EEA20)
T6_VOICE_GUARD(824EEA78)
T6_VOICE_GUARD(824EEAD8)
T6_VOICE_GUARD(824EEB30)
T6_VOICE_GUARD(824EEB90)
T6_VOICE_GUARD(824EEBD8)
T6_VOICE_GUARD(824EEC30)
T6_VOICE_GUARD(824EEC98)
T6_VOICE_GUARD(824EECF0)
T6_VOICE_GUARD(824EED68)
T6_VOICE_GUARD(82583FA0)
T6_VOICE_GUARD(82583C98)
std::atomic<uint64_t> g_voice_made{0}, g_voice_failed{0}, g_voice_freed{0}, g_voice_free_ignored{0};
struct VoiceInfo {
  uint32_t slot, lr, frame, loads;
  bool resim, main;
  uint32_t snd_addr = 0, snd_lr = 0;  // the recorded game -> library call it was created in (0 = outside one)
};
std::unordered_map<uint32_t, VoiceInfo> g_voices;  // guarded by g_dead_mutex
// release of a voice / of an MMC library object: dead from now on (a second release from a re-simulated frame is ignored)
extern "C" void __imp__sub_824EE840(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824EE840(PPCContext& ctx, uint8_t* base) {
  if (DeadCall(ctx, base, 0x824EE840u, 0)) {
    g_voice_free_ignored++;
    return;
  }
  const uint32_t v = ctx.r3.u32, rel_lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_824EE840(ctx, base);
  if (ctx.r3.u32 == 0) {
    g_voice_freed++;
    {
      std::lock_guard lock(g_dead_mutex);
      g_voices.erase(v);
    }
    if (LifeOn()) {
      std::lock_guard lock(g_life_mutex);
      if (const auto it = g_life.find(v); it != g_life.end()) {
        const SndLife& l = it->second;
        const double now = T6NowMs();
        std::fprintf(g_life_file, "%08X,%d,%u,%u,%d,%08X,%08X,%08X,%.1f,%u,%u,%u,%08X,%08X,%08X,%d,%.1f,%u,%u,%d,%08X,%d,%s\n", v,
                     T6NetRunning() ? 1 : 0, l.f0, l.mode0, l.resim ? 1 : 0, l.snd_addr, l.snd_lr, l.lr, now - l.t0,
                     l.last_state, l.n36, l.n40, l.lr40, l.call40, l.call40_lr, l.resim40 ? 1 : 0, l.n40 ? now - l.t40 : -1.0, g_net_frame,
                     T6GameMode(), g_net_resim ? 1 : 0, rel_lr, g_life_orphan ? 1 : 0, [&] {
                       static char hex[129];
                       for (int k = 0; k < 64; k++) std::snprintf(hex + 2 * k, 3, "%02X", l.entry[k]);
                       return hex;
                     }());
        std::fflush(g_life_file);  // (the launcher ends the game process: nothing may wait in a buffer)
        g_life.erase(it);
      }
    }
  }
  if (VoiceGuardOn() && ctx.r3.u32 == 0) SetDead(v, true);
}
extern "C" void __imp__sub_82583C88(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82583C88(PPCContext& ctx, uint8_t* base) {
  if (DeadCall(ctx, base, 0x82583C88u, 0)) return;
  const uint32_t v = ctx.r3.u32;
  __imp__sub_82583C88(ctx, base);
  if (VoiceGuardOn()) SetDead(v, true);
}
// get voice of an MMC library object: a dead object hands out no voice
extern "C" void __imp__sub_82583FB0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82583FB0(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r4.u32;
  if (DeadCall(ctx, base, 0x82583FB0u, 0x88960001u)) {
    if (out) *reinterpret_cast<uint32_t*>(base + out) = 0;
    return;
  }
  __imp__sub_82583FB0(ctx, base);
}
// creators: the address is live again
// (voice accounting: creates, failed creates with the HRESULT, final releases - a create that fails leaves the sound
// entry with a null voice = that sound is mute; log line "T6 voices" per stats window)
// Live voices: where the creator stored the pointer (the CnuSound entry's +4, sub_82576FA8) and when. A voice whose
// slot no longer holds it is an ORPHAN: nobody will release it (leak; user's log 2026-10-12: 20 -> 229 live voices
// over five matches, in bursts when rollbacks start after a round intro; at the limit every create fails = mute).
uint64_t T6NetLoads();
void T6VoiceCreated(uint32_t fn, uint32_t out, PPCContext& ctx, uint8_t* base, uint32_t lr) {
  if (ctx.r3.s32 < 0) {
    if (T6LogSample(g_voice_failed, 20, 500)) {
      REXLOG_WARN("T6 net: voice create {:08X} failed {:08X} (lr {:08X}, frame {}), {} so far; {} created, {} released", fn, ctx.r3.u32, lr,
                  g_net_frame, g_voice_failed.load(), g_voice_made.load(), g_voice_freed.load());
    }
    return;
  }
  g_voice_made++;
  if (!out) return;
  const uint32_t v = T6Rd32(out);
  std::lock_guard lock(g_dead_mutex);
  g_voices[v] = {out, lr, g_net_frame, static_cast<uint32_t>(T6NetLoads()), g_net_resim, t6_thread_bit == 0, g_snd_cur_addr, g_snd_cur_lr};
  if (LifeOn()) {
    std::lock_guard life_lock(g_life_mutex);
    SndLife& l = g_life[v];
    l = SndLife{};
    l.t0 = T6NowMs();
    l.f0 = g_net_frame;
    l.mode0 = T6GameMode();
    l.snd_addr = g_snd_cur_addr;
    l.snd_lr = g_snd_cur_lr;
    l.lr = lr;
    l.resim = g_net_resim;
    if (out >= 4 && PageReadable((out - 4) >> 12)) std::memcpy(l.entry, base + out - 4, 64);
  }
  if (VoiceGuardOn()) g_dead.erase(v);
}
// Voices orphaned by a rollback (vl3, 2026-10-12: the CnuSound voice entries sit in the game heap, AB9F60xx, stride
// 0x40 - a snapshot load rewinds them, the voices themselves are live). A sound whose voice was created in a discarded
// frame: the load empties its entry, the next live sound update creates a second voice for the same request, and the
// first one is held by nobody - it plays on and is never released. 22 orphans in 7200 frames locally; the user's
// matches: 20 -> 229 live voices, then every create fails and the game is mute. Fix: the voices their slot held
// before the load and does not hold after the whole rollback (at the end of the boundary: a replayed sound call of
// a re-simulated frame puts its voice back - releasing right after the load cut ~120 such sounds short, vl4) are
// released there (the sound then starts again with the voice of
// the next live frame instead of playing twice).
// fade_from: 0 = plays on to its end (user 2026-10-17: "proste neusekavej zvuky"), else the frame its fade began (the same
// sound was started again by the corrected frames - the new voice goes on, no double)
struct PendingOrphan { uint32_t v, id10, id18, frame; float base; uint32_t fade_from = 0; };  // (base: the volume it had, voice +140)
std::vector<PendingOrphan> g_orphans_pending;  // (main thread only)
constexpr int kOrphanFade = 5;  // frames of the fade of a wrong prediction's sound (~83 ms), then released
uint64_t g_orphans_adopted = 0, g_orphans_faded = 0, g_orphans_played_out = 0;
extern "C" void __imp__sub_824EE9C8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824EE840(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824EECF0(PPCContext& ctx, uint8_t* base);
// the orphan v goes on in the entry slot of nv (the same sound started again), nv is released
void T6OrphanTakeOver(uint32_t v, uint32_t nv, uint32_t slot, uint32_t id10, uint32_t id18) {
  if (!g_main_ctx) return;
  // (the volume the entry asked for: the voice wrapper keeps it at +140)
  uint32_t vol_bits = 0x3F800000u;
  if (PageReadable((nv + 140) >> 12)) vol_bits = T6Rd32(nv + 140);
  float vol;
  std::memcpy(&vol, &vol_bits, 4);
  PPCContext tmp = *g_main_ctx;
  tmp.r3.u64 = v;
  tmp.f1.f64 = vol;
  sub_824EECF0(tmp, g_t6_membase);
  *reinterpret_cast<uint32_t*>(g_t6_membase + slot) = _byteswap_ulong(v);
  t6_dirty_mark(slot);
  {
    std::lock_guard lock(g_dead_mutex);
    if (const auto it = g_voices.find(nv); it != g_voices.end()) {
      VoiceInfo info = it->second;
      g_voices.erase(it);
      g_voices[v] = info;
    }
  }
  tmp = *g_main_ctx;
  tmp.r3.u64 = nv;
  sub_824EE840(tmp, g_t6_membase);  // the newer voice goes (the hook counts it)
  if (g_orphans_adopted++ < 40)
    REXLOG_INFO("T6 net: sound {:X}/{:X} started again at frame {} after a rollback: voice {:08X} goes on, newer {:08X} dropped ({} so far)",
                id10, id18, g_net_frame, v, nv, g_orphans_adopted);
}
// a voice was just created into the entry slot 'out' (live sound update): the same sound as a pending orphan?
void T6OrphanAdopt(uint32_t out, PPCContext& ctx, uint8_t* base) {
  static const bool takeover = T6Env("T6_ORPHAN_TAKEOVER") != nullptr;  // (off since 2.2.1, see T6VoicesAfterLoad)
  if (g_orphans_pending.empty() || ctx.r3.s32 < 0 || out < 4 || !PageReadable((out - 4) >> 12) || g_net_resim) return;
  const uint32_t e = out - 4, nv = T6Rd32(out), id10 = T6Rd32(e + 0x10), id18 = T6Rd32(e + 0x18);
  if (!takeover) {
    // the corrected frames started the same sound again: the orphan fades out over kOrphanFade frames (the new voice
    // plays it) - not twice, and not cut: the sound goes on in the new voice
    for (auto& o : g_orphans_pending)
      if (!o.fade_from && o.id10 == id10 && o.id18 == id18 && o.v != nv) {
        o.fade_from = g_net_frame;
        return;
      }
    return;
  }
  if (static int n = 0; T6Env("T6_ORPHAN_DIAG") && n++ < 200)
    REXLOG_INFO("T6 orphan diag: frame {} created {:08X} ids {:X}/{:X} with {} pending", g_net_frame, nv, id10, id18, g_orphans_pending.size());
  for (size_t k = 0; k < g_orphans_pending.size(); k++) {
    const PendingOrphan o = g_orphans_pending[k];
    if (o.id10 != id10 || o.id18 != id18 || o.v == nv) continue;
    g_orphans_pending.erase(g_orphans_pending.begin() + k);
    T6OrphanTakeOver(o.v, nv, out, id10, id18);
    return;
  }
}
// every live frame boundary: an orphan that plays on is released once it has finished playing (GetState 0x20; at
// the latest after kOrphanMax frames); one whose sound started again fades out over kOrphanFade frames, then goes
constexpr int kOrphanMax = 600;  // (10 s: no sound of the fight is longer; a voice nobody holds must not stay for ever)
uint64_t g_orphans_gone = 0;
// the orphan is no longer ours: released meanwhile (dead / out of the voice list), its address created again and held
// by a slot, or no valid voice there. A player's log 2026-10-17 (2.2.4, twice vs Lars): the GetState below ran on a released
// voice (vtable 0) -> guest read of 0x30 in sub_824EE9C8, the game crashed (calls straight to __imp__ skip DeadCall).
bool T6OrphanGone(uint32_t v) {
  {
    std::lock_guard lock(g_dead_mutex);
    const auto it = g_voices.find(v);
    if (it == g_voices.end() || g_dead.count(v)) return true;
    const uint32_t slot = it->second.slot;
    if (slot && PageReadable(slot >> 12) && T6Rd32(slot) == v) return true;
  }
  PPCContext tmp = *g_main_ctx;
  tmp.r3.u64 = v;
  return StaleVoice(tmp, g_t6_membase, 0x824EE9C8u);
}
void T6OrphansTick() {
  if (g_orphans_pending.empty() || !g_main_ctx) return;
  static const bool old_fade = T6Env("T6_ORPHAN_FADE") != nullptr;  // T6_ORPHAN_FADE=1: every orphan fades (2.2.3)
  static uint32_t state_out = g_t6_sys_alloc ? g_t6_sys_alloc(16, 16) : 0;  // (the byte GetState writes)
  for (size_t k = 0; k < g_orphans_pending.size();) {
    PendingOrphan& o = g_orphans_pending[k];
    if (T6OrphanGone(o.v)) {
      if (g_orphans_gone++ < 20)
        REXLOG_WARN("T6 net: orphan voice {:08X} (frame {}) gone before its release at frame {}, dropped", o.v, o.frame, g_net_frame);
      g_orphans_pending.erase(g_orphans_pending.begin() + k);
      continue;
    }
    const int age = static_cast<int>(g_net_frame) - static_cast<int>(o.frame);
    PPCContext tmp = *g_main_ctx;
    tmp.r3.u64 = o.v;
    if (!old_fade && !o.fade_from && age >= 0) {
      bool done = age > kOrphanMax || !state_out;
      if (!done) {
        PPCContext q = *g_main_ctx;
        q.r3.u64 = o.v;
        q.r4.u64 = state_out;
        g_t6_membase[state_out] = 0;
        __imp__sub_824EE9C8(q, g_t6_membase);
        done = (g_t6_membase[state_out] & 0x20) != 0;
      }
      if (!done) {
        k++;
        continue;
      }
      g_orphans_pending.erase(g_orphans_pending.begin() + k);
      g_life_orphan = true;
      sub_824EE840(tmp, g_t6_membase);
      g_life_orphan = false;
      g_orphans_played_out++;
      continue;
    }
    const int fage = o.fade_from ? static_cast<int>(g_net_frame) - static_cast<int>(o.fade_from) : age;
    if (age < 0 || fage > kOrphanFade || o.base < 0) {  // (age < 0: a new session; base < 0: volume unknown - released)
      g_orphans_pending.erase(g_orphans_pending.begin() + k);
      g_life_orphan = true;
      sub_824EE840(tmp, g_t6_membase);
      g_life_orphan = false;
      g_orphans_faded++;
      continue;
    }
    if (fage > 0) {
      // (2.2.0 faded from 1.0: quiet sounds - steps, crowd, a wall scrape - got loud for a moment, a player's
      // note "background sound spikes". Now from the volume the voice had: the SetVolume wrapper keeps it at +140.)
      tmp.f1.f64 = o.base * (1.0 - static_cast<double>(fage) / kOrphanFade);
      sub_824EECF0(tmp, g_t6_membase);
    }
    k++;
  }
}
struct HeldVoice { uint32_t v, slot, id10, id18; };  // (the entry's +0x10 / +0x18 = which sound, so1 2026-10-15)
std::vector<HeldVoice> g_voices_held;  // (voice, slot) before the load
uint64_t g_voice_orphans_released = 0;
bool g_voices_held_valid = false;
void T6VoicesBeforeLoad() {
  if (g_voices_held_valid) return;  // (a second load in the same boundary: the first picture counts)
  g_voices_held_valid = true;
  g_voices_held.clear();
  std::lock_guard lock(g_dead_mutex);
  for (const auto& [v, i] : g_voices) {
    if (PageReadable(i.slot >> 12) && T6Rd32(i.slot) == v) {
      const uint32_t e = i.slot - 4;
      g_voices_held.push_back({v, i.slot, T6Rd32(e + 0x10), T6Rd32(e + 0x18)});
    }
  }
}
extern "C" void sub_824EE840(PPCContext& ctx, uint8_t* base);
void T6VoicesAfterLoad() {
  static const bool on = T6EnvDefaultOn("T6_VOICE_ORPHANS");  // =0: old behaviour
  if (!g_voices_held_valid) return;
  g_voices_held_valid = false;
  if (!on || !g_main_ctx) return;
  for (const auto& [v, slot, id10, id18] : g_voices_held) {
    if (T6Rd32(slot) == v) continue;
    // Not cut any more (user 2026-10-15: "usekava hit soundy", "at tamten necha dohrat", "nechci aby hral zdvojene"):
    // the orphan waits a few frames. Does the corrected run start the same sound again (the next live sound update
    // creates a voice for an entry with the same +0x10 / +0x18 - so1: the hit landed a frame later), the new voice is
    // dropped and the orphan goes on in its entry (T6OrphanAdopt): no restart, no double. Else it was the sound of a
    // wrong prediction: faded out over kOrphanFade frames, then released (T6OrphansTick). T6_ORPHAN_KEEP=0: old cut.
    if (static const bool keep = T6EnvDefaultOn("T6_ORPHAN_KEEP"); keep) {
      // the same sound already started again by the corrected frames (sf1: mostly 1-3 frames after the orphan, before
      // this rollback ends): that newer voice is dropped and the orphan, which plays longer, takes its entry
      // OFF since 2.2.1 (T6_ORPHAN_TAKEOVER=1 = on): the live run's writes put back by the sound update replay of a later
      // rollback gave the entry the dropped voice again - the orphan was held by nobody, the dropped one got 1600+
      // ignored calls (a player's log 2026-10-15)
      static const bool takeover = T6Env("T6_ORPHAN_TAKEOVER") != nullptr;
      uint32_t twin = 0, twin_slot = 0;
      if (takeover) {
        std::lock_guard lock(g_dead_mutex);
        const auto made = g_voices.find(v);
        const uint32_t f0 = made != g_voices.end() ? made->second.frame : g_net_frame;
        for (const auto& [w, i] : g_voices) {
          if (w == v || i.frame < f0 || i.frame > f0 + 6 || !PageReadable(i.slot >> 12) || T6Rd32(i.slot) != w) continue;
          if (T6Rd32(i.slot - 4 + 0x10) == id10 && T6Rd32(i.slot - 4 + 0x18) == id18) {
            twin = w;
            twin_slot = i.slot;
            break;
          }
        }
      }
      if (twin) {
        T6OrphanTakeOver(v, twin, twin_slot, id10, id18);
        continue;
      }
      float base = -1.0f;
      if (PageReadable((v + 140) >> 12)) {
        const uint32_t bits = T6Rd32(v + 140);
        float f;
        std::memcpy(&f, &bits, 4);
        if (std::isfinite(f) && f >= 0.0f && f <= 4.0f) base = f;
      }
      g_orphans_pending.push_back({v, id10, id18, g_net_frame, base});
      if (static int n = 0; T6Env("T6_ORPHAN_DIAG") && n++ < 60)
        REXLOG_INFO("T6 orphan diag: frame {} orphan {:08X} ids {:X}/{:X} pending, volume {}", g_net_frame, v, id10, id18, base);
      continue;
    }
    VoiceInfo made{};
    {
      std::lock_guard lock(g_dead_mutex);
      if (const auto it = g_voices.find(v); it != g_voices.end()) made = it->second;
    }
    PPCContext tmp = *g_main_ctx;
    tmp.r3.u64 = v;
    g_life_orphan = true;
    sub_824EE840(tmp, g_t6_membase);  // (the hook: counts it, marks the address dead)
    g_life_orphan = false;
    if (g_voice_orphans_released++ < 40 || tmp.r3.u32 != 0) {
      REXLOG_INFO("T6 net: voice {:08X} orphaned by the rollback at frame {} released ({} references left), {} so far; created at frame {}{} "
                  "by {:08X} in the sound call {:08X} from {:08X}", v, g_net_frame, tmp.r3.u32, g_voice_orphans_released, made.frame,
                  made.resim ? " (re-simulated)" : "", made.lr, made.snd_addr, made.snd_lr);
    }
  }
}
void T6VoiceStatsLog(uint32_t frame) {
  size_t dead = 0, orphans = 0, held = 0;
  std::string first;
  {
    std::lock_guard lock(g_dead_mutex);
    dead = g_dead.size();
    for (const auto& [v, i] : g_voices) {
      const bool mapped = PageReadable(i.slot >> 12);
      const uint32_t now = mapped ? T6Rd32(i.slot) : 0;
      if (now == v) { held++; continue; }
      orphans++;
      if (orphans <= 6) {
        first += fmt::format(" {:08X}(slot {:08X} now {:08X}, lr {:08X}, frame {}{}{}, load {})", v, i.slot, now, i.lr, i.frame,
                             i.resim ? " resim" : "", i.main ? "" : " other thread", i.loads);
      }
    }
  }
  REXLOG_INFO("T6 voices: frame {}: {} created, {} released ({} live), {} creates failed, {} releases ignored, {} dead addresses; "
              "{} held by their slot, {} orphans ({} released after loads){}{}",
              frame, g_voice_made.load(), g_voice_freed.load(), g_voice_made.load() - g_voice_freed.load(), g_voice_failed.load(),
              g_voice_free_ignored.load(), dead, held, orphans, g_voice_orphans_released, orphans ? ":" : "", first);
}
#define T6_VOICE_CREATE(addr)   extern "C" void __imp__sub_##addr(PPCContext& ctx, uint8_t* base);   extern "C" void sub_##addr(PPCContext& ctx, uint8_t* base) {     const uint32_t out = ctx.r4.u32, lr = static_cast<uint32_t>(ctx.lr);     __imp__sub_##addr(ctx, base);     T6VoiceCreated(0x##addr##u, out, ctx, base, lr);     T6OrphanAdopt(out, ctx, base);   }
T6_VOICE_CREATE(824EEFC0)
T6_VOICE_CREATE(824EF020)
extern "C" void __imp__sub_82341C88(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82341C88(PPCContext& ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_82341C88(ctx, base);
  if (VoiceGuardOn() && self) {
    if (const uint32_t obj = T6Rd32(self + 284)) SetDead(obj, false);
  }
}

// Get state (vtbl[48]) of a voice into the byte at r4: a dead voice reports "finished" (0x20), so the CnuSound entry
// that still holds it (sub_82576F50: state & 0x20 -> sub_82576EE8 = release + entry cleared) frees its slot on the next
// update instead of calling the dead voice every frame for ever (sv4: thousands of ignored calls, a mute slot).
extern "C" void __imp__sub_824EE9C8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824EE9C8(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r4.u32;
  if (DeadCall(ctx, base, 0x824EE9C8u, 0)) {
    if (out) {
      base[out] = 0x20;
      t6_dirty_mark(out);
    }
    return;
  }
  const uint32_t v = ctx.r3.u32;
  __imp__sub_824EE9C8(ctx, base);
  if (LifeOn() && out) {
    std::lock_guard lock(g_life_mutex);
    if (const auto it = g_life.find(v); it != g_life.end()) it->second.last_state = base[out];
  }
}

// Every queue the game waits on (see QueuesDrained) is known to the save / load from its first flush.
extern "C" void __imp__sub_823BA530(PPCContext& ctx, uint8_t* base);
extern "C" void sub_823BA530(PPCContext& ctx, uint8_t* base) {
  const uint32_t q = ctx.r3.u32;
  if (q - 0x82726000u < 0x68A000u) {  // static data only (a heap object can be gone when the save looks at it)
    std::lock_guard<std::mutex> lock(g_drain_mutex);
    if (std::find(g_drain_queues.begin(), g_drain_queues.end(), q) == g_drain_queues.end()) {
      g_drain_queues.push_back(q);
      REXLOG_INFO("T6 snaptest: work queue {:08X} (lock {:08X}, wake {:08X}) joins the idle check of saves / loads", q,
                  T6Rd32(q + 44),
                  T6Rd32(q + 40));
    }
  }
  __imp__sub_823BA530(ctx, base);
}

// Steady state of tcb_device_sound_t, sub_82340600 = { sub_82571FD8(16); return 0; } = the sound library's frame update
// (handle requests, entries, voice calls under the library lock): 6 ms in a re-simulated frame, 1.5 ms live. PS3 never
// ran the sound device task in re-simulated frames. Skipped while re-simulating since the dead-voice handling (ns1 6207
// frames / 474 rollbacks, rs2, rs3: fight state equal, no crash): game step of a re-simulated frame 15.5 -> 10 ms.
extern "C" void __imp__sub_82340600(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82340600(PPCContext& ctx, uint8_t* base) {
  static const bool skip = T6Env("T6_RESIM_SOUND") == nullptr;  // T6_RESIM_SOUND=1: run it in re-simulated frames
  if (skip && g_net_resim && t6_thread_bit == 0) {
    // The update is not run - but what the live run of this frame wrote into the game memory (the voice entries in
    // the game heap) is put back: the library is not rolled back, so after the load the entries have to say again
    // what the library really did. Without it a voice created by the update of a discarded frame lost its entry, the
    // next live update created a second one (the sound started twice) and the first was cut as an orphan (vo1: 4 of
    // 11 orphans created in 82571FD8 from 82340614). T6_SND_DEV_REPLAY=0 = off.
    static const bool dev_replay = T6EnvDefaultOn("T6_SND_DEV_REPLAY");
    if (dev_replay) T6SndReplayDeviceUpdate(base);
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82340600(ctx, base);
}
