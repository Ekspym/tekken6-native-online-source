#pragma once
// Tekken 6 Native Online launcher - This PC: CPU, graphics cards, machine id, region, local address, the match found sound.

#include "common.h"

void playMatchFoundSound(const QString& file = QString());
QString cpuMissing();

// Graphics adapters of this PC that can run the game (hardware, Direct3D 12): index in EnumAdapters1 order = the
// game's --d3d12_adapter. autoGpu = what the game takes with -1 (the high performance one, see d3d12_provider.cpp).
struct Gpu
{
	int index;
	QString name;
};
QList<Gpu> gpuList();
QString autoGpu();
QString machineId();
QString guessRegion();
QString localIp();
