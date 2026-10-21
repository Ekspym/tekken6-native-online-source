// Tekken 6 - the practice's HUD panels (user 2026-10-20, "frame data trochu vic detailni stylem jako to ma T8" + "input
// display"): the frame data of the last attack (top right) and the input display (left, under the practice help).
// Drawn by T6FrameDataDialog (tekken6_app.cpp) on the guest output thread; the values come from t6_frame_data.cpp /
// t6_practice.cpp through atomics and the input rows' mutex.

#include "tekken6_app.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {
constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;

// The move struct's attack level word (+0x10, the Tekken 7 move layout: vulnerability +0xC, level +0x10, cancels +0x14).
// Bits 8 - 11, seen 2026-10-20 (Jin, Steve, Hwoarang, Wang, Leo dumps and lvl_* runs): 5 high (jabs, Hwoarang f+4),
// 2 mid (df+1, df+3, Wang ws4), 1 low (d+4, d+3), 4 special low (d+1, d+2); 0x00400000 = throw (1+3: 0x00400512).
// 3 = special mid is the Tekken order but was not seen yet.
struct Level {
  const char* name;
  ImVec4 color;
};
Level LevelOf(uint32_t w, float a) {
  if (w & 0x00400000u) return {"THROW", ImVec4(0.78f, 0.55f, 1.0f, a)};
  switch ((w >> 8) & 0xF) {
    case 5: return {"HIGH", ImVec4(1.0f, 0.85f, 0.3f, a)};
    case 2: return {"MID", ImVec4(0.4f, 0.8f, 1.0f, a)};
    case 1: return {"LOW", ImVec4(1.0f, 0.45f, 0.6f, a)};
    case 4: return {"SPECIAL LOW", ImVec4(1.0f, 0.6f, 0.35f, a)};
    case 3: return {"SPECIAL MID", ImVec4(0.45f, 0.95f, 0.8f, a)};
    default: return {nullptr, ImVec4(0.8f, 0.8f, 0.85f, a)};
  }
}

// one row of the panel: the label grey on the left, the value in its colour at a fixed column
void Row(const char* label, const ImVec4& grey, float col, const ImVec4& color, const char* fmt, ...) {
  ImGui::TextColored(grey, "%s", label);
  ImGui::SameLine(col);
  va_list args;
  va_start(args, fmt);
  ImGui::TextColoredV(color, fmt, args);
  va_end(args);
}

// an arrow of the input display (forward = right, as the move lists write it), or a small dot for neutral
void Arrow(ImDrawList* dl, ImVec2 c, float r, uint8_t dirs, ImU32 col) {
  float x = ((dirs & 8) ? 1.0f : 0.0f) - ((dirs & 4) ? 1.0f : 0.0f);
  float y = ((dirs & 2) ? 1.0f : 0.0f) - ((dirs & 1) ? 1.0f : 0.0f);
  if (x == 0.0f && y == 0.0f) {
    dl->AddCircleFilled(c, r * 0.18f, IM_COL32(150, 150, 160, 200));
    return;
  }
  const float n = std::sqrt(x * x + y * y);
  x /= n;
  y /= n;
  const ImVec2 tip(c.x + x * r, c.y + y * r), tail(c.x - x * r * 0.8f, c.y - y * r * 0.8f);
  const ImVec2 base(c.x + x * r * 0.15f, c.y + y * r * 0.15f);
  dl->AddLine(tail, base, col, r * 0.32f);
  const float w = r * 0.55f;
  dl->AddTriangleFilled(tip, ImVec2(base.x - y * w, base.y + x * w), ImVec2(base.x + y * w, base.y - x * w), col);
}
}  // namespace

// One side's box (Tekken 8's frame data box), on screen all the time while its menu row is on (user 2026-10-20: "aby
// frame data opponenta byli toggle a mohli byt zobrazeny furt"): the side's last attack - level, startup, active
// frames, damage - and the frame advantage of the last exchange from this side's view: its own attack's number (on
// hit / on block, KD, recovery on whiff; PUNISHABLE from -10 on block), or minus the other side's when that one is
// newer (the opponent's box shows -8 after your +8 jab, as in Tekken 8).
static void DrawSide(ImGuiIO& io, int side) {
  const int other = 1 - side;
  const uint32_t at = g_t6_fa_tick[side].load(), sat = g_t6_fa_startup_tick[side].load(), oat = g_t6_fa_tick[other].load();
  // the advantage belongs to the attack whose startup shows: only a number measured after that startup (opp2
  // 2026-10-20: the jab's startup showed with the d+4 before it: "-4 on hit")
  const bool own = at && (!sat || static_cast<int32_t>(at - sat) >= 0);
  // the other side's number is newer: this side was the defender (a whiff of the other side is not this side's)
  const uint32_t oflag = g_t6_fa_flag[other].load();
  const bool theirs = oat && oflag != 0xFFFFFFFFu && (!at || static_cast<int32_t>(oat - at) > 0) &&
                      (!sat || static_cast<int32_t>(oat - sat) > 0);
  const int v = theirs ? -g_t6_fa_value[other].load() : g_t6_fa_value[side].load();
  const uint32_t flag = theirs ? oflag : g_t6_fa_flag[side].load();
  const bool has_hit = theirs || own;
  const float scale = io.DisplaySize.y / 720.0f;
  const ImVec4 grey(0.75f, 0.75f, 0.8f, 1.0f), white(1.0f, 1.0f, 1.0f, 1.0f), green(0.35f, 0.95f, 0.45f, 1.0f), red(1.0f, 0.4f, 0.35f, 1.0f);
  // (where the user drew them, Pictures\pozice.PNG 2026-10-20: this player's box right of the input display, the
  // opponent's on the right, both a little under the middle of the screen)
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * (side ? 0.955f : 0.14f), io.DisplaySize.y * 0.55f), ImGuiCond_Always,
                          ImVec2(side ? 1.0f : 0.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.6f);
  if (ImGui::Begin(side ? "##t6framedata2" : "##t6framedata", nullptr, kPanel)) {
    ImGui::SetWindowFontScale(scale * 1.1f);
    ImGui::TextColored(grey, side ? "OPPONENT" : "YOU");
    ImGui::SetWindowFontScale(scale * 1.5f);
    const float col = ImGui::GetFontSize() * 6.5f;
    if (sat) {
      const uint32_t w = g_t6_fa_level[side].load();
      const Level lv = LevelOf(w, 1.0f);
      if (lv.name) ImGui::TextColored(lv.color, "%s", lv.name);
      else ImGui::TextColored(lv.color, "LEVEL %X", (w >> 8) & 0xF);
      Row("Startup", grey, col, white, "i%d", g_t6_fa_startup[side].load());
      if (const uint32_t span = g_t6_fa_active[side].load()) {
        const int a0 = static_cast<int>(span >> 16), a1 = static_cast<int>(span & 0xFFFF);
        Row("Active", grey, col, white, a0 == a1 ? "%d" : "%d-%d", a0, a1);
      }
      if (const int d = g_t6_fa_damage[side].load()) Row("Damage", grey, col, white, "%d", d);
    } else {
      ImGui::TextColored(grey, "no attack yet");
    }
    if (has_hit) {
      const char* what = flag == 0xFFFFFFFEu ? "On hit" : flag == 0xFFFFFFFFu ? "Whiff" : (flag & 1u) ? "On block" : "On hit";
      ImGui::Spacing();
      ImGui::TextColored(grey, "%s", what);
      ImGui::SameLine(col);
      ImGui::SetWindowFontScale(scale * 2.6f);
      // (0xFFFFFFFE = knockdown / launch on hit: no number - green for the attacker, red for the one knocked down;
      // 0xFFFFFFFF = the attack whiffed: v is minus its recovery)
      if (flag == 0xFFFFFFFEu) ImGui::TextColored(theirs ? red : green, "KD");
      else if (flag == 0xFFFFFFFFu) ImGui::TextColored(grey, "%d", -v);
      else ImGui::TextColored(v > 0 ? green : v < 0 ? red : white, "%+d", v);
      ImGui::SetWindowFontScale(scale * 1.5f);
      if (flag == 0xFFFFFFFFu) ImGui::TextColored(grey, "recovery frames");
      else if ((flag & 1u) && v <= -10) ImGui::TextColored(red, theirs ? "PUNISH!" : "PUNISHABLE");
    }
  }
  ImGui::End();
}

// Each side its own box (user 2026-10-20: "kazdej vlastni box ... moje vlevo dole a jeho vpravo dole jako to dela
// tekken 8"), each with its own menu row
void T6DrawFrameData(ImGuiIO& io, uint32_t) {
  if (g_t6_pr_cursor.load() >= 0) return;  // (not over our open menu)
  for (int side = 0; side < 2; side++)
    if (g_t6_fa_display[side].load()) DrawSide(io, side);
}

// The player's inputs (Tekken 8's input display): newest at the top, frames held, arrow, buttons 1 - 4
void T6DrawInputDisplay(ImGuiIO& io) {
  if (!g_t6_in_display.load() || g_t6_pr_cursor.load() >= 0) return;  // (not over our open menu)
  std::vector<T6InputRow> rows;
  {
    std::lock_guard lock(g_t6_in_mutex);
    rows.assign(g_t6_in_rows.begin(), g_t6_in_rows.end());
  }
  if (rows.empty()) return;
  const float scale = io.DisplaySize.y / 720.0f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.36f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.45f);
  if (ImGui::Begin("##t6inputs", nullptr, kPanel)) {
    ImGui::SetWindowFontScale(scale * 1.4f);
    const float h = ImGui::GetFontSize() * 1.25f, r = h * 0.38f;
    const float num_w = ImGui::CalcTextSize("999").x;
    // buttons the Tekken way (user 2026-10-20: "zobrazit 4 kolecka a vybarvit jen ty co se zmackli"): four small
    // circles in a square next to the arrow - 1 top left, 2 top right, 3 bottom left, 4 bottom right - filled in the
    // button's colour when pressed, an empty ring when not
    static const ImU32 kBtn[4] = {IM_COL32(70, 140, 255, 255), IM_COL32(255, 205, 60, 255), IM_COL32(80, 210, 110, 255),
                                  IM_COL32(240, 80, 80, 255)};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t k = 0; k < rows.size(); k++) {
      const T6InputRow& row = rows[k];
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImU32 text = k == 0 ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 210, 220);
      char num[8];
      std::snprintf(num, sizeof(num), "%u", row.frames);
      dl->AddText(ImVec2(p.x + num_w - ImGui::CalcTextSize(num).x, p.y + (h - ImGui::GetFontSize()) * 0.5f), text, num);
      const float ax = p.x + num_w + h * 0.75f, cy = p.y + h * 0.5f;
      Arrow(dl, ImVec2(ax, cy), r, row.dirs, text);
      const float bx = ax + h * 1.05f, d = h * 0.24f, br = h * 0.19f;
      for (int b = 0; b < 4; b++) {
        const ImVec2 c(bx + ((b & 1) ? d : -d), cy + ((b & 2) ? d : -d));
        if (row.buttons & (1 << b)) dl->AddCircleFilled(c, br, kBtn[b]);
        else dl->AddCircle(c, br, IM_COL32(150, 150, 160, 150), 0, 1.0f);
      }
      ImGui::Dummy(ImVec2(num_w + h * 2.1f, h));
    }
  }
  ImGui::End();
}
