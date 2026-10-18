#pragma once
// Tekken 6 Native Online launcher - The package folder: the game, its build state, the settings file, the reference profile, shader storage.

#include "common.h"

QString root();
QString rootFile(const QString& rel);
QString gameExe();
QString gameDataRoot();
QString builderVersion();

// What the player's game needs after an update of the package (never asks for the ISO again):
//   GameOk      native\tekken6.exe is up to date
//   GameRelink  only the prebuilt parts changed (builder\prebuilt.sha != native\prebuilt.sha): link again, seconds
//   GameUpdate  the game code changed (build.version): codegen + only the changed files from native\game, ~1 min
//   GameBuild   no game data / no objects of an earlier build: build from the copy (ISO)
enum GameNeed { GameOk, GameRelink, GameUpdate, GameBuild };
GameNeed gameNeed();
QSettings settings();
QStringList filesUnder(const QString& dir);
QString buildHash();
bool freshProfile(const QString& to);
void addShaderCache(const QString& runDir);
