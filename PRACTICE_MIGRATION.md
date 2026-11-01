# Native practice migration

Source: JalenDmarion25/tekken6-recomp `feature/practice-mode`,
`4fc20f166f5fda1ac05ec0d306ec8b0d9bfe4698`.
Destination base: Ekspym/tekken6-native-online-source,
`24db272e03ab0e6c35f22abea519e76b22ecf450`.

This is an implementation for Windows integration testing, not a verified release.
The Windows game, launcher and distributable builder must be rebuilt together.
A new game executable has a different online compatibility hash.

## Result

The native practice menu extensions replace the destination's BACK overlay menu,
three-slot recorder, block-after-hit shortcuts and snapshot position tools.
The port includes Quick Select, native position/side/wall resets and Rage,
eight 30-second recording slots, default weight 10, weighted looping playback,
side-aware inputs, opponent-change clearing, native prompt placement, Racing Sans
One navy HUDs and the current AIRBORNE RECOVERY V3 startup/advantage engine.
Optional input history is retained; platform controller remapping and rollback
implementation remain in the destination modules.

Product features default on for offline play and `T6_PRACTICE` launches.
There is no need to set the old eight TEST feature flags. Capture/probe flags do
not default on. The launcher's existing frame-data checkbox controls the new
frame-data HUD. `T6_ADVANCED_PRACTICE=0` disables the port; the removed legacy menu
is not restored by this switch.

Manual character/stage selection is preserved outside Quick Select. The launcher
still routes the initial offline menu to Practice, then releases input to the
player. It reveals the practice window when a validated selector is ready and
keeps the process alive across Character Select and Return to Main Menu.
Launcher character/stage overrides are bypassed in this offline path.

## Isolation

55 guarded hooks are added; the existing three resolution hooks remain single.
Input/memory hooks check offline availability, live session state and rollback
state. Generated menu functions retain exact original bodies and dispatch to
them outside the practice port. New HUDs share the existing guest-output drawer,
with their font installed in that drawer's own font atlas.

The pinned destination SDK and its existing netplay patches remain the SDK used
by this port. No source SDK replacement, generated guest C++, game files or
runtime binaries are distributed with the migration.

## Loading speed

Normal Character Select and Stage Select keep ordinary pacing. Acceleration starts
only after a validated Quick Select Apply request, covering its automated native
reload. The destination SDK's `rex_audio_fast` export drops audio submissions and
releases audio pacing slots; vsync is temporarily disabled during that reload.
The prior vsync value and normal audio pacing are restored before the new practice
fight, on completion/cancellation/disable/session boundary, or after an eight-second
wall-clock timeout checked at game-frame boundaries. Intermediate teardown of the
old practice object does not stop an accepted Quick Select reload. The guest clock
and recordings are not scaled. Missing API/vsync support leaves ordinary pacing.
`TEKKEN6_FAST_QUICK_SELECT=0` disables the acceleration. The previous
`TEKKEN6_FAST_MANUAL_TRANSITIONS` switch is no longer used.

## Build paths

Builder version 4 forces regeneration for existing version-3 installs; even a
manual `--relink` upgrades through codegen when these versions differ. The font
license is staged beside the executable in direct and packaged builds.

CMake adapts build copies of generated menu sources after codegen. The packaged
builder applies the same adapter after the four existing netplay patch tools.
`tools/make_builder.py` includes the adapter/header and recognizes the new build
copy paths. Regenerate a distributable's prebuilt archive/config together; copying
only the new Python builder into an old package is insufficient.

Use the destination README's toolchain/SDK preparation and build instructions.
Its staged MinGW build must contain these new sources and config before building.
For a first CMake configuration with no generated guest code, build
`tekken6_codegen`, configure again, then build `tekken6`; menu adaptation is
activated on that second configuration. Existing patched generated code should
be regenerated rather than combined with another row-storage adapter.

## Checks completed here

- C++23 syntax checks of the migrated practice translation unit against the
  destination SDK headers, with the Windows/netplay facade declarations replaced
  by test declarations; new HUD syntax checked against ImGui headers.
- Frame-recovery/stance/string engine assertions and transition-policy assertions.
- All 16 audited menu functions and 66 storage/capacity adaptations validated.
- All 17 adapted functions (including selection rendering) preserve exact original
  fallbacks; adapter idempotency checked.
- Full four-tool netplay patch pipeline followed by row adaptation passed against
  the local reference generated source; no reference game code is included here.
- 62 unique configured hook addresses; production include closure checked.

## Required Windows acceptance tests

1. Build the SDK/game and launcher; rebuild the distributable builder if used.
2. Launch practice through the launcher: select both characters and stage manually.
   Check selection is visible/controllable and the fight runs at ordinary speed.
3. Test Mode Select, Quick Select in every practice mode, reset toggle/side/walls,
   Rage, Character Select and Return to Main Menu.
4. Record all eight slots, overwrite a slot, finish with Start and by timeout,
   test weights/zero exclusion/loop stop, side swaps, mode gating and opponent clear.
5. Check navy/Racing Sans One panels and native prompt alignment. Check jab hit
   and block advantage and Hwoarang u+4, ws 4,4, RFF f+3 and both 3~4 variants.
6. Test an online match, resimulation, rematch, return to lobby and controller
   remapping on two matching builds. Verify no migrated practice menu/input or
   acceleration is active online.
7. Compare Quick Select reloads with acceleration on/off, plus canceled
   selection and a slow loading case; verify pacing and audio restore correctly.

Windows compilation, interactive gameplay and online regression tests could not
be run in this Linux workspace. These checks are necessary before merging.
