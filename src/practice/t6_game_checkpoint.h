#pragma once
namespace t6np {
struct SnapTest;
// Standalone complete images; the online incremental ring keeps its normal path.
void SavePracticeImage(SnapTest& image);
bool LoadPracticeImage(const SnapTest& image);
}
