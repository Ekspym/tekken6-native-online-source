#pragma once
// Tekken 6 Native Online launcher - Release versions and the files an update keeps.

#include "common.h"

bool testChannel();
qint64 versionKey(const QString& tag);
bool sameFile(const QString& a, const QString& b);
bool keptByUpdate(const QString& rel);
