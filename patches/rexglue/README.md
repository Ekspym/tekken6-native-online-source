# ReXGlue patches

The project's changes to the ReXGlue SDK, applied to a fresh checkout of the `third_party/rexglue-sdk` submodule with
`python tools/apply_rexglue_patches.py` (after `git submodule update --init --recursive`):

1. `0001-tekken6-sdk-changes.patch` - against `rexglue-sdk` `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: code generation
   options of the game, netplay runtime support (deterministic audio stepping, lock-free waits, exports the game code
   uses for snapshots), the presenter's guest output overlay, the llvm-mingw build and the fused resolve shader.
2. `0002-ffmpeg-mingw.patch` - against the SDK's `thirdparty/FFmpeg` at `0604b464c7cb4ebc94940cf1f324a3b26b87717c`:
   the llvm-mingw (UCRT) configuration.

The tool refuses another revision or a checkout with local changes, and does nothing when a patch is already applied.
