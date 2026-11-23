# Changelog

## 0.1.0 (unreleased, 2026-11-22)

First playable version: the Xbox USA disc of *Harry Potter and the Chamber of Secrets*, built on the player's Mac. Local only so
far; not released.

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
- Known limits: only the Burrow, Diagon Alley and Borgin & Burkes have been played; a box in the alley behind Borgin &
  Burkes would not move for the owner (not reproduced yet); 4:3 only (no widescreen); the PAL disc is not supported.
