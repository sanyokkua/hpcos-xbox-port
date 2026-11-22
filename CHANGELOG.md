# Changelog

## 0.1.0 (unreleased, 2026-11-22)

First playable version: the Xbox USA disc of *Harry Potter and the Chamber of Secrets*, built on the player's Mac. Local only so
far; not released.

- Base: the engine of the Harry Potter and the Sorcerer's Stone port (sanyokkua/hp1-xbox-port v0.1.0), which is the NFS
  Underground 2 port's engine (xboxrecomp, nfsu2-sw, nfsu2-ios's Apple host and Metal renderer, NFSU2Deadware's Mac patches).
  This game's facts (177 hidden functions, the XInput functions, the movie overlay, the logo skip, the file-cache race) from
  HP-COS-Xbox-Recomp; the cutscene fast-forward design from HP2-Recompiled.
- The game: the logos, the intro movie, the title, New Game and the save-slot screen, loading, the Burrow intro cutscene with
  subtitles and speech, free play at the Burrow (Harry walks and casts, the HUD, the Flipendo tutorial, Fred, George and Ron),
  the pause menu and the game's Options, sound. 60 fps at 3x on an M1 Pro.
- Engine work for this game (details in `docs/PORTING.md`): XDK 5028 glue found by byte signatures; missed functions seeded; the
  chip's callback registers latched; the movies drawn from the NV2A video overlay by a new layer in the Metal backend; viewport
  scale and offset also loaded as transform constants; a fog unit in the vertex shaders; opaque texture views for
  no-alpha render targets; skinned characters get the screen offset; no copy of a render-target page the combiners never read
  (24 fps to 60 fps); movie sound read from the right memory (the PCM movies screeched); a hold in the file cache that makes a load hang rare; one function patched after every lift.
- In-game settings menu (Back + Start, F1 or Cmd+,): Display, Graphics (anti-aliasing, anisotropic filtering, bloom and ambient
  occlusion, gamma), Audio, Input (vibration, keyboard), Tweaks and Advanced. Tweaks: Skip Logo Movies, Cutscene Fast-Forward
  (hold Start or Back in a cutscene: it plays to its end about 8x as fast, muted, nothing left out).
- Controller with rumble and keyboard, through XInput functions after HP-PS-Xbox-Recomp; a radial dead zone on the sticks.
- Everything the port is built from is in the repository (`engine/`): no download at build time.
- The app keeps everything in `~/Library/Application Support/Harry Potter Chamber of Secrets`; logs in `logs/`.
- Installer DMG (5.2 MB, no game code): imports the disc image, checks `default.xbe` by its section digests, lifts and compiles
  the game with Apple's Command Line Tools, puts the app in Applications. macOS 15+. A clean install took 1 min 27 s.
  `tests/dmg_no_game_code.sh` passes (2,263 disc files, 64 code windows).
- Known limits: saving and loading are not tested (the game saves at story points, which the tests did not reach); nothing
  past the Burrow is tested; 4:3 only (no widescreen); the PAL disc is not supported; a rare "disc is dirty" screen (seen once
  in about 70 boots, an invalid-handle read on the music stream, under investigation); a load hang at the first loading screen
  still happens now and then (about 1 boot in 30 under load, down from 1 in 8): restart the game if it stays on "loading".
