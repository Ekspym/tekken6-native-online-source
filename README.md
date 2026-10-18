# Tekken 6 Native Online

A native PC port of Tekken 6 (Xbox 360) built with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static
recompiler, with rollback netcode (a GGPO fork) and a launcher for ranked / casual matchmaking, lobbies and practice.

**No game code or data is in this repository.** The player builds the game on their own PC from their own copy of
Tekken 6 (USA / Europe, Rev 1, Xbox 360): the launcher's builder translates the disc's `Default.xex` to C++, applies the
netplay patches and compiles it. Generated code, the game image, extracted data and save data never belong here.

## Layout

- `src/` - the game side: netplay (rollback snapshots, input, sound boundary, determinism), launcher status /
  commands, HUD name plates, practice frame data, internal resolution.
- `launcher/` - the Qt launcher (matchmaking UI, game builder front end, controls, updates).
- `builder/` - `build_native.py`, the build the player's package runs (codegen, patch tools, compile, link).
- `third_party/ggpo/` - GGPO with the project's changes (MIT, see its LICENSE).
- `third_party/rexglue-sdk/` - the SDK (submodule); `patches/rexglue/` holds the project's SDK changes.
- `tools/` - the patch tools of the generated code and development / test scripts.

## Building

1. `git submodule update --init --recursive`, then `python tools/apply_rexglue_patches.py`.
2. Put your own extracted game into `game/` (or let the launcher's builder do it from an ISO).
3. Game: `build_x360.cmd` (clang + MSVC ABI) or `build_mingw.cmd` (llvm-mingw, the package build).
4. Launcher: `launcher\build.bat` (Qt 6, MSVC).

The lobby server is not part of this repository. The official build's server address, release repository, pinned
server certificate, rank pictures and launcher art come from `launcher/official.cmake` and `launcher/official.qrc`,
which are not published; without them the launcher builds with placeholders (`T6_SERVER`, `T6_RELEASE_API` in
`launcher/CMakeLists.txt`) and draws text instead of the art.

## Credits

- Based on [mahmoud-mess/tekken6-recomp](https://github.com/mahmoud-mess/tekken6-recomp) (the original ReXGlue setup
  of the game, the SDK patches, the internal resolution and speed-up work), used with the author's permission.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD 3-Clause, derived from Xenia).
- [GGPO](https://github.com/pond3r/ggpo) (MIT).
- Bai Jamjuree font (SIL Open Font License, `launcher/fonts/OFL-BaiJamjuree.txt`), Material Symbols icons (Apache 2.0,
  `launcher/icons/LICENSE.txt`).

Tekken is a trademark of Bandai Namco Entertainment. This project is not affiliated with or endorsed by Bandai Namco.
