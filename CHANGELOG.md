# Changelog

## 0.1.0 (2026-11-22)

First release: the Xbox USA disc of *Harry Potter and the Sorcerer's Stone*, built on the player's Mac.

- Base: the engine of the NFS Underground 2 port (nfsu2-sw with nfsu2-ios's Apple host and Metal renderer, NFSU2Deadware's Mac
  patches), with its NFSU2-only parts removed; this game's facts from HP-PS-Xbox-Recomp.
- The game: the logos, the intro, the menus, New Game through Diagon Alley, the Hogwarts arrival and the first missions at
  Hogwarts; saves and loading; controller (with rumble) and keyboard.
- Engine fixes for this game: one thread object and id per game thread (the loader's paths), x87 compare flags for `lahf` (black
  cutscenes), functions hidden behind jump tables seeded, fixed-function lighting and pre-lit rooms on Metal, a surface no longer
  rebuilt mid-frame (black menus and message boxes), projected shadows (texture generation, the texture matrix, no copies behind
  the light), the kernel import scan.
- In-game settings menu (Back + Start, F1 or Cmd+,): Display, Graphics (anti-aliasing, anisotropic filtering, bloom and ambient
  occlusion, gamma), Audio, Input (vibration, keyboard), Tweaks and Advanced. Tweaks: Skip Logo Movies, Skip Cutscenes (story
  cuts skipped, scenes inside a level fast-forwarded and silent), Skip Save Messages (no pause on the storage messages).
- A radial dead zone on the sticks (pads that rest off centre no longer push the camera or Harry).
- Everything the port is built from is in the repository (`engine/`): no download at build time.
- The app keeps everything in `~/Library/Application Support/Harry Potter Sorcerer's Stone`; logs in `logs/`.
- Installer DMG (no game code): imports the disc image, checks `default.xbe` by its section digests (images differ in a few
  media-patched bytes), lifts and compiles the game with Apple's Command Line Tools, puts the app in Applications. macOS 15+.
