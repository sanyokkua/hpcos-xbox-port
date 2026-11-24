# Changelog

## Unreleased

Engine and tool fixes backported from the Prisoner of Azkaban port (no change to the lift of this disc, no visible change):

- Disassembler: a call target that splits an instruction is also dropped when its only callers are bodies decoded out of data.
  Relift of this disc: `functions.json` identical (6,131 starts).
- Kernel: `ExQueryNonVolatileSetting` answers `XC_FACTORY_AV_REGION` (0x103) as NTSC-M (0x00400100), matching the fixed North America game region.
- `tools/stubcheck.py` also covers the XDK code sections; bytes past a section's raw size count as data. This disc: 13 stubs, 0 real code.
- Metal shaders compile as MSL 3.0 (`RECOMP_MTL_LANG` overrides); the hitch log counts pipelines.
- Stick range compensation learned per direction instead of a fixed 0.65 (a 65 % push no longer reads as full; `RECOMP_STICK_RANGE=0.65` restores it).
- `tools/immscan.py`: finds functions referenced only by immediates (0 on this disc).

## 0.1.0 (2026-11-24)

First release: the Xbox USA disc of *Harry Potter and the Chamber of Secrets*, built on the player's Mac. Played from the
logos through the Burrow, Knockturn Alley, Diagon Alley and its shops, the Leaky Cauldron and the Whomping Willow to the first
day at Hogwarts; saving and loading work.

- Base: the engine of the Harry Potter and the Sorcerer's Stone port (sanyokkua/hp1-xbox-port v0.1.0), which is the NFS
  Underground 2 port's engine (xboxrecomp, nfsu2-sw, nfsu2-ios's Apple host and Metal renderer, NFSU2Deadware's Mac patches).
  This game's facts (177 hidden functions, the XInput functions, the movie overlay, the logo skip, the file-cache race) from
  HP-COS-Xbox-Recomp; the cutscene skip design from HP2-Recompiled.
- The game: the logos, the intro movie, the title, New Game and the save-slot screen, loading, the Burrow intro cutscene with
  subtitles and speech, free play at the Burrow (Harry walks and casts, the HUD, the Flipendo tutorial, Fred, George and Ron),
  the pause menu and the game's Options, sound. 60 fps at 3x on an M1 Pro.
- Engine work for this game (details in `docs/PORTING.md`): XDK 5028 glue found by byte signatures; missed functions seeded; the
  chip's callback registers latched; the movies drawn from the NV2A video overlay by a new layer in the Metal backend; viewport
  scale and offset also loaded as transform constants; a fog unit in the vertex shaders; opaque texture views for
  no-alpha render targets; skinned characters get the screen offset; no copy of a render-target page the combiners never read
  (24 fps to 60 fps); movie sound read from the right memory (the PCM movies screeched); a hold in the file cache that makes a load hang rare; one function patched after every lift.
- In-game settings menu (Back + Start, F1 or Cmd+,): Display, Graphics (anti-aliasing, anisotropic filtering, bloom and ambient
  occlusion, gamma), Audio, Input (vibration, keyboard), Tweaks and Advanced. Tweaks: Skip Logo Movies, Skip Cutscenes
  (press Start or Back in a cutscene: it runs to its end in a few seconds, about 30x, muted, nothing left out).
- Controller with rumble and keyboard, through XInput functions after HP-PS-Xbox-Recomp; a radial dead zone on the sticks.
- Everything the port is built from is in the repository (`engine/`): no download at build time.
- The app keeps everything in `~/Library/Application Support/Harry Potter Chamber of Secrets`; logs in `logs/`.
- Installer DMG (5.2 MB, no game code): imports the disc image, checks `default.xbe` by its section digests, lifts and compiles
  the game with Apple's Command Line Tools, puts the app in Applications. macOS 15+. A clean install took 1 min 27 s.
  `tests/dmg_no_game_code.sh` passes (2,263 disc files, 64 code windows).
- After the owner's first play test (2026-11-23): W-buffer depth (windows no longer flicker against their walls); ambient
  occlusion reads it as linear depth (no more haze over the sky, no dark halos sliding along walls); no line through the
  title picture; Skip Cutscenes on a single press and about 30x (was 8x on paper, ~1.3x in practice); LB / RB are White / Black (were swapped); short presses are no
  longer lost; a crash at boot (a GPU callback overwritten before it ran) and the "disc is dirty" screen after skipping the intro
  fixed; load stalls rarer (1 boot in 36 in tests, open).
- Second play test: save scrolls could do nothing (the save slot reset after End Day; now kept, still to confirm); the
  boot / loading-screen stall's cause found and fixed (loader callbacks published their state before their result);
  left stick without a second dead zone, with range compensation.
- Third play test: saving works. A SAVE GAME scroll asks for a save through a function the lifter had missed (0x4DD30: it
  copies the game state into the slot and opens the save screen); it was an empty stub, so the scroll did nothing. Now
  lifted: a save is written (`game/UDATA/4541000e/…/POTTER A`) and Continue on the title loads it (tested: save in the
  Burrow, Continue → "Potter A – The Burrow"). A new disc import keeps the saves. Every press reaches the game for at least
  two reads (~33 ms; `RECOMP_PAD_MIN_READS`).
- Fourth play test: Climb, Grab and Pull now come up from every side. The lifter read 80-bit floats (`fld tbyte`) as
  4-byte ones, so the C runtime's asin/acos returned about 0 instead of ±π/2 for an argument of exactly ±1. Harry's heading
  comes from the stick through it, so with the stick pointing straight along an axis (pushing into a crate or wall) he
  faced the wrong way and the action's facing test failed. Found with a new differential test (`tools/difftest.py`:
  the original x86 run in Unicorn from a memory snapshot, compared with the lifted code).
- Fifth play test: no more shadows sliding over walls and floors. Characters cast their shadows through a shadow map the
  game draws each frame around Harry, with a grey border colour (its "no shadow" value). Metal samplers only have black
  or white borders, so the port used white: everything outside the map was lit twice as bright, and the map's edges
  showed as dark bands moving with the camera. The pixel shader now returns each stage's own border colour.
- Sixth play test (2026-11-24): the crash or freeze after opening a door (Leaky Cauldron cellar, Knockturn Alley) is
  fixed. Picking up a Bertie Bott's bean or a coin ran code whose shared exit the lifter had missed and replaced with an
  empty stub: the game's entity update came back with its stack and loop registers wrong, then called through garbage
  and crashed or hung at the next door. All 47 such missed functions are lifted now, and the build stops if a stub is
  ever real code again (`tools/stubcheck.py`). Also from this test's audit: a skipped wild call keeps the stack
  balanced; file handles are held while in use and closed once; reads at an offset are atomic; a timer can no longer
  lose its wake-up; a crate's drops are not removed twice after a level streams (HP-COS's guard).
- Seventh play test (2026-11-24): 46 minutes from Diagon Alley to Hogwarts with no wild call and no crash. Screenshots of
  five more places. A clean install from the DMG found one more crash: when a controller disconnected (a pad going to
  sleep), the game's thread looked for pads again while the main thread did the same, and the app aborted (sdl2-compat keeps
  its pad list without a lock). Pads are now opened and closed on the main thread only; the game's thread just reads them.
  The keyboard's Q / E are White / Black again (they had stayed on the old, swapped bumpers).
- Known limits: the rest of the school year has not been played on the Mac; 4:3 only (no widescreen); the PAL disc is not
  supported.
