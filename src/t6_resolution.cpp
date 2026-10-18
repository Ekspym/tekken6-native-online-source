#include <cstdint>
#include "t6_common.h"
#include <cstdlib>
#include <cstring>

#include <rex/ppc/context.h>

namespace {

struct InternalResolution {
  uint32_t width;
  uint32_t height;
};

InternalResolution GetSelectedResolution() {
  const char* value = T6Env("TEKKEN6_INTERNAL_RESOLUTION");
  if (value == nullptr) {
    return {1024, 576};
  }
  if (std::strcmp(value, "640x360") == 0) {
    return {640, 360};
  }
  if (std::strcmp(value, "768x432") == 0) {
    return {768, 432};
  }
  return {1024, 576};
}

}  // namespace

// Internal 3D resolution (from github mahmoud-mess/tekken6-recomp, 2026-10-17; launcher Profile -> display:
// TEKKEN6_INTERNAL_RESOLUTION 640x360 / 768x432 / 1024x576, TEKKEN6_OFFSCREEN_RESOLUTION 512x512 / 1024x1024).
// Invoked by generated mid-assembly hooks immediately before the guest stores
// its internal render dimensions. Unrecognized or absent values keep the
// retail 1024x576 mode.
void ApplyTekkenInternalResolution(PPCRegister& width, PPCRegister& height) {
  static const InternalResolution resolution = GetSelectedResolution();
  width.u32 = resolution.width;
  height.u32 = resolution.height;
}

// The game creates this offscreen texture independently of the main scene
// resolution. Keep the override limited to its known guest resource ID.
void ApplyTekkenOffscreenResolution(PPCRegister& resource_id, PPCRegister& width,
                                   PPCRegister& height) {
  if (resource_id.u32 != 0x20000004) {
    return;
  }

  static const InternalResolution resolution = [] {
    const char* value = T6Env("TEKKEN6_OFFSCREEN_RESOLUTION");
    if (value != nullptr && std::strcmp(value, "512x512") == 0) {
      return InternalResolution{512, 512};
    }
    return InternalResolution{1024, 1024};
  }();
  width.u32 = resolution.width;
  height.u32 = resolution.height;
}
