// Tekken 6 netplay - research of the "twisted characters" (players: Lili 2026-10-18, Jin on a clip of a ranked match:
// a character drawn upside down / in impossible poses on and off for ~13 s while the fight goes on normally)
//
// T6_POSE_DIAG=1, two questions:
// 1. Does the pose of a character depend on memory that only the draw writes? The draw subtree (sub_82218FA0) is not
//    walked in re-simulated fight frames (sub_8221CA68 rule in t6_resim.cpp), so such memory is left as the load put
//    it (k frames old) when the first live frame after a rollback draws. T6PoseDiagDraw(true / false) around the draw
//    subtree walk of live fight frames: the pages it writes (dirty map cleared around the walk, then put back), every
//    600 live frames "T6 pose draw pages" with the runs, those inside character task objects marked.
// 2. Is the character state the same on both sides frame by frame? T6PoseDiagSave(frame) at every save (cb_save) of
//    the fight: a hash of the character task objects (tcb_charmanager_t subtree types, 0x400 bytes each) -> "T6 pose
//    hash" for every 10th frame once it is confirmed; compare the two instances' logs by frame. Equal hashes with a twisted picture = the
//    draw-only state of question 1; different hashes = character state that the snapshot does not bring back.
// Character task types (RTTI of the image, vtable at the node): created by tcb_charmanager_t 8200C06C (its vtable slot
// 8200C07C = sub_822304C8, parent = the manager), stepped in re-simulated frames too (tprof1: 5 per frame both ways).

#include "t6_netplay_internal.h"

namespace {
constexpr uint32_t kCharVt[] = {
    0x8200C06Cu,  // tcb_charmanager_t
    0x8200BF54u,  // tcb_chars_t
    0x8200C29Cu,  // tcb_charmotion_t
    0x8200C148u,  // tcb_charmodel_t
    0x82000FD4u,  // tcb_kkCharSkeleton
    0x82001034u,  // tcb_kkBunshinSkeleton
    0x820017B4u,  // tcb_kkPlayerSkeleton
    0x820074C4u,  // tcb_kwModelLayer_t
    0x82005B90u,  // kwBoneOptRoot
    0x820063E4u,  // CSaveMatrixDrawTcb
};
constexpr uint32_t kObjBytes = 0x400;

bool On() {
  static const bool on = T6Env("T6_POSE_DIAG") != nullptr;
  return on;
}

// the character task objects in the task tree (root *(0x82828C60), links +16..+40 like T6CrowdNodes), sorted
std::vector<uint32_t> CharNodes() {
  std::vector<uint32_t> out, todo{T6Rd32(0x82828C60u)};
  std::unordered_set<uint32_t> seen;
  while (!todo.empty() && seen.size() < 20000) {
    const uint32_t n = todo.back();
    todo.pop_back();
    if (!T6HeapPtr(n) || !seen.insert(n).second) continue;
    const uint32_t v = T6Rd32(n);
    for (const uint32_t c : kCharVt)
      if (v == c) out.push_back(n);
    for (uint32_t o = 16; o <= 40; o += 4) todo.push_back(T6Rd32(n + o));
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<unsigned char> g_keep;     // the frame's dirty map while the draw subtree runs
std::map<uint32_t, uint32_t> g_pages;  // page -> live frames whose draw subtree wrote it
uint32_t g_frames = 0;
}  // namespace

void T6PoseDiagDraw(bool begin) {
  if (!On() || !T6NetRunning() || g_net_resim || t6_thread_bit != 0 || T6GameMode() != 4) return;
  if (begin) {
    g_keep.assign(t6_dirty, t6_dirty + sizeof(t6_dirty));
    std::memset(t6_dirty, 0, sizeof(t6_dirty));
    return;
  }
  if (g_keep.empty()) return;
  for (uint32_t q = 0; q < sizeof(t6_dirty); q++) {
    if (t6_dirty[q]) g_pages[q]++;
    t6_dirty[q] |= g_keep[q];
  }
  g_keep.clear();
  if (++g_frames % 600) return;
  const auto nodes = CharNodes();
  const auto in_char = [&](uint32_t lo, uint32_t hi) {
    for (const uint32_t n : nodes)
      if (n < hi && n + kObjBytes > lo) return true;
    return false;
  };
  std::string runs;
  for (auto it = g_pages.begin(); it != g_pages.end();) {
    const uint32_t a0 = it->first;
    uint32_t e = a0 + 1, most = it->second;
    for (++it; it != g_pages.end() && it->first == e; ++it, e++) most = std::max(most, it->second);
    runs += fmt::format(" {:08X}-{:08X}x{}{}", a0 << 12, e << 12, most, in_char(a0 << 12, e << 12) ? "(char)" : "");
  }
  std::string objs;
  for (const uint32_t n : nodes) objs += fmt::format(" {:08X}={:08X}", n, T6Rd32(n));
  REXLOG_INFO("T6 pose draw pages over {} live frames: {} pages:{}", g_frames, g_pages.size(), runs);
  REXLOG_INFO("T6 pose char objects:{}", objs);
  g_pages.clear();
}

// pd3: the effect a model layer points to (+0x15C, target of its tcb_pointer_t<kwModelLayerEffect_Base> at +0x154) sat
// at different addresses on the two sides in bursts. Every live frame: a target whose first word is not one of the
// kwModelLayerEffect_* vtables nor tcb_pointer_t<ZSGEDataManageTcb> 8200D0F0 (pd4: what the target holds when valid) = a pointer to an object a rollback discarded or reused -> "T6 pose stale effect".
static void StaleEffectCheck(const std::vector<uint32_t>& nodes) {
  static constexpr uint32_t kEffectVt[] = {0x8200D0F0u, 0x82007374u, 0x820073ACu, 0x820073E4u, 0x8200741Cu, 0x82007454u, 0x8200748Cu};
  static uint32_t stale = 0, checked = 0;
  for (const uint32_t n : nodes) {
    if (T6Rd32(n) != 0x820074C4u) continue;  // tcb_kwModelLayer_t
    const uint32_t e = T6Rd32(n + 0x15C);
    if (!e) continue;
    checked++;
    const uint32_t vt = T6HeapPtr(e) ? T6Rd32(e) : 0;
    if (std::find(std::begin(kEffectVt), std::end(kEffectVt), vt) != std::end(kEffectVt)) continue;
    if (++stale <= 50)
      REXLOG_INFO("T6 pose stale effect: frame {} layer {:08X} effect {:08X} first word {:08X} (+0x50 {:08X})", g_net_frame, n, e, vt,
                  T6Rd32(n + 0x50));
  }
  if (g_net_frame % 600 == 0) REXLOG_INFO("T6 pose effects: {} checked, {} stale", checked, stale);
}

void T6PoseDiagFrame() {
  if (!On() || !T6NetRunning() || g_net_resim || T6GameMode() != 4) return;
  static std::vector<uint32_t> nodes;
  static uint32_t found_at = 0;
  if (nodes.empty() || g_net_frame < found_at || g_net_frame - found_at >= 300) {
    nodes = CharNodes();
    found_at = g_net_frame;
  }
  StaleEffectCheck(nodes);
}

// pd1-pd6 sampled the hashes at live frame boundaries, where the remote inputs may still be predicted: those samples
// differ between the sides by design. Now like NoteFightState: noted at every save (live and re-simulated, a later save
// of the same frame replaces the row), written once the frame is 10 behind (confirmed, prediction max 3).
void T6PoseDiagSave(uint32_t frame) {
  if (!On() || T6GameMode() != 4 || frame % 10) return;
  static std::vector<uint32_t> nodes;
  static uint32_t found_at = 0;
  if (nodes.empty() || frame < found_at || frame - found_at >= 300) {
    nodes = CharNodes();
    found_at = frame;
  }
  // T6_POSE_DIAG=2: also the words of every object -> <T6_NET_DUMP_DIR>/pose_words.txt (one line per object and frame)
  static FILE* words = [] {
    const char* dir = T6Env("T6_NET_DUMP_DIR");
    return T6EnvIs("T6_POSE_DIAG", '2') && dir ? std::fopen((std::string(dir) + "/pose_words.txt").c_str(), "w") : nullptr;
  }();
  static std::map<uint32_t, std::pair<std::string, std::string>> rows;  // frame -> (hash line, word lines)
  auto& [line, w] = rows[frame];
  line.clear();
  w.clear();
  for (const uint32_t n : nodes) {
    if (!T6InGameRange(n, kObjBytes)) continue;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t o = 0; o < kObjBytes; o += 4) h = (h ^ T6Rd32(n + o)) * 1099511628211ull;
    line += fmt::format(" {:08X}:{:016X}", T6Rd32(n), h);
    if (words) {
      w += fmt::format("{} {:08X}", frame, n);
      for (uint32_t o = 0; o < kObjBytes; o += 4) w += fmt::format(" {:08X}", T6Rd32(n + o));
      w += '\n';
    }
  }
  while (!rows.empty() && rows.begin()->first + 10 <= frame) {
    REXLOG_INFO("T6 pose hash frame {}:{}", rows.begin()->first, rows.begin()->second.first);
    if (words) std::fputs(rows.begin()->second.second.c_str(), words);
    rows.erase(rows.begin());
  }
  if (words) std::fflush(words);
}
