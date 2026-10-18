#pragma once
// Tekken 6 Native Online launcher - Game processes: ended together with the launcher, leftovers of an earlier launcher.

#include "common.h"

void endWithLauncher(qint64 pid);

// At start (one launcher per PC): a game of this package's tekken6.exe that still runs was left by a launcher that went
// away (before endWithLauncher) - it would hold the game's UDP port. Returns how many were ended.
inline int g_leftoverGames = 0;
int endLeftoverGames();
