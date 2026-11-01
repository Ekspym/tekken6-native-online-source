#pragma once
#include <rex/ppc/context.h>
struct ImGuiIO;
struct ImFontAtlas;
bool T6AdvancedRequested();
bool T6AdvancedAvailable();
extern "C" bool T6AdvancedGuestAvailable();
void T6AdvancedTick();
bool T6AdvancedManualSelection();
void T6AdvancedConfigureFonts(ImFontAtlas* atlas);
void T6AdvancedDraw(ImGuiIO& io);
const char* T6PracticeEnv(const char* name);

// Session preference and render-thread-safe practice visibility snapshot.
bool T6FrameDataEnabled(unsigned player);
void T6SetFrameDataEnabled(unsigned player,bool enabled);
bool T6FrameDataVisible(unsigned player);

// Position checkpoint shortcuts and render-thread-safe footer status.
void T6PositionSnapshotPad(uint32_t user,uint8_t* pad);
bool T6PositionSnapshotAvailable();
unsigned T6PositionSnapshotStatus();
