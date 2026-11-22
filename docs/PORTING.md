# How the port was made

A log of the steps, the decisions and what went wrong. It follows the NFS Underground ports
([nfsu2-xbox-port](https://github.com/sanyokkua/nfsu2-xbox-port), [nfsu-xbox-port](https://github.com/sanyokkua/nfsu-xbox-port)),
which were made first with the same engine; this file says where Harry Potter differs. A PS2 port of the same game was tried
before this one; its recompiler's instruction bugs (sticks, floors, timing) made the Xbox version, on a proven stack, the better
path.

## Sources (pinned)

| What | Where | Pin |
|---|---|---|
| Engine, overlay menu, installer | [sanyokkua/nfsu2-xbox-port](https://github.com/sanyokkua/nfsu2-xbox-port) | `d23c105` (v0.1.0): xboxrecomp, nfsu2-sw and nfsu2-ios (`85d2cbf`), NFSU2Deadware's patches |
| This game's facts | [MiddleDucky/HP-PS-Xbox-Recomp](https://github.com/MiddleDucky/HP-PS-Xbox-Recomp) (MIT, Windows, D3D8 on D3D12) | `f475af9c` |
| Disc | Xbox USA, `default.xbe` title `0x45410037`, XDK 5659, entry `0x153DD1`, `.text` 1,570,612 bytes | section digests in `mac/xbe_check.py` |

HP-PS-Xbox-Recomp plays the game from start to finish on Windows. Its hand-written addresses are for the **European** executable:
in the library sections after `.text` (D3D, DSOUND, XPP, data) the USA address is the European one + 0x20; `.text` functions
shift by -0x5E0..+0x8E0, so they were matched by bytes.

## 1. The disc

- An XISO (partition at 0): `default.xbe` (3.2 MB), 75 `.hog` archives, 18 `.xmv` movies, `disk.txt`; 1.8 GB.
- As on the NFS Underground disc, the image's `.text` doesn't match its own stored digest (a media patch by the tool that made
  the image), so the check compares the title, each section's size and stored digest, and every other section's data
  (`mac/xbe_check.py`, 13 sections).

## 2. Lift, build, the XDK glue

- **Lift:** the NFS Underground 2 port's `regen.sh` with HP1's executable: 59 s, about 6,000 functions, 27 C files. The XDK
  functions the engine hooks (memmove, `D3D_BlockOnTime`, the vblank and PGRAPH interrupt handlers, the DSOUND DSP post, the
  AC'97 reset) were found in HP1's executable by masked byte signatures of the same functions in the NFSU1 and NFSU2 executables
  (`tools/sigmatch.py`; both agreed, each hit unique). Linked the first time.
- **NFSU2-only parts removed** from the engine: the Switch and iOS hosts, the VP6 movie player (HP1 decodes its own XMV movies),
  the HUD mover, the network card, the text patch, the texture and prompt packs.

## 3. Bring-up, in the order the bugs appeared

Each was a black screen, a hang or a crash.

1. **Cutscene stuck on its loading screen:** the loader built every path as `\Device\CdRom0\(null)a001_0001.banr`. XAPI keeps a
   per-thread table keyed by the thread id; the runtime gave every thread one shared thread object and no id. Now each thread has
   its own object and id (`xbox_memory_layout.c`, `kernel_bridge.c`).
2. **Black cutscene after loading:** `fucompi; lahf; test ah, 0x44; jp` picks "draw the scene or black". The lifter built `lahf`
   only after SSE compares. Now x87 compares set the flags `lahf` reads.
3. **Menu invisible:** its per-frame callback sits right after a jump table, which the disassembler read as code, so the
   function was missing (`[ICALL] Failed to resolve VA 0x00023BD0` every frame). Seeded (`engine/config/seed_functions.json`).
   Later crashes of the same kind (Ollivanders, the Flipendo scene) were fixed the same way; one bad seed in the middle of an
   instruction cut a function in two and blacked out the Hogwarts arrival for 90 s: check every seed is an instruction start.
4. **The controller never opened:** replaced XAPI's XInput with HP-PS-Xbox-Recomp's direct design, fed from the runtime's pad
   report (so the keyboard, scripted input and the menu's neutral pad still apply). Rumble completes at once: the game waited
   forever the first time Harry was hit.
5. **White walls, then black rooms:** the Metal renderer had no fixed-function lighting (the NFS games use vertex programs).
   Added the chip's lighting per vertex, and the emission from the vertex colour for HP1's pre-lit rooms.
6. **Black Remembrall, hint boxes and save screens:** the game drew them, but the renderer rebuilt and cleared the screen surface
   mid-frame (the 3D scene and the UI came with different clip widths). Now a surface is reused or grown with its pixels kept.
7. **Tiled shadows, then mirrored ones:** HP1 projects character shadows with texture-coordinate generation and a texture
   matrix, which the renderer passed through as plain coordinates (a copy of the shadow on every floor plank). Both are in the
   vertex shader now; behind the projector (q <= 0) the lookup falls outside the texture, so no mirrored wedges.
8. **The kernel import scan overwrote code:** the scan before the import table took the XPP section's bytes for 7 imports. Only
   ordinals below 512 now: 110 of 110 imports bridged.

## 4. Port features

- **Settings menu** (the NFS ports' overlay): HP1's rows in `engine/src/overlay/hp1_options.c`. Bloom needed a lower "this is a 3D
  scene" threshold (HP1's rooms have 80-170 draws, the NFS games' 150+): `NFSU2_POST_MIN_DRAWS=40`.
- **Skip Logo Movies** (from HP-PS-Xbox-Recomp): the movie player returns at once for the logos; the boot reads only a flag,
  which stays as if the movie ran to its end.
- **Skip Cutscenes:** the game's own cuts (`cut/*.hog`) have a skip check that the override answers "pressed" once per cut (the
  arrival cut asks twice per update). Scenes inside a level (letterboxed level scripts) have no skip: they run 8 game ticks per
  frame, muted, with lines of speech stopped. The wand shuffle in Ollivanders steps per drawn frame and gets an 8x step too. A
  try at an 8x step for everything per frame sent the game back to the title at the end of Diagon Alley and was reverted. Each
  fast-forward logs its length; over 4 s it names the script command that held it.
- **Skip Save Messages:** the storage checks hold each step at least 500 or 750 ms in busy loops on a stopwatch (as on the
  console); the stopwatch answers "long enough" to exactly those loops (matched by their bytes).
- **Stick dead zone:** pads rest off centre (a tester's at -1800 / +800 of 32767); a radial dead zone of ~11% with the rest
  rescaled to full range (`engine/xboxrecomp/src/input/stick_deadzone.h`, unit test in `engine/xboxrecomp/tests/stick_deadzone`).

## 5. A self-contained repository and the installer

As in the NFS Underground ports: `engine/` holds everything, `tools/kit.py` collects the lifter and the compiled runtime (no game
code) into the installer, `mac/build_game.sh` lifts, compiles and links on the player's Mac, `tests/dmg_no_game_code.sh` checks the
DMG. HP1 changes: the target `hp1_recomp`, the `HP1_` installer variables, the disc check, and the test's game file patterns
(`*.hog`, `*.xmv`). The test's 64-byte code windows now skip padding: HP1's executable sections hold zero runs that match any
binary (SDL, Capstone) and gave false failures; a DMG with a 400 KB slice of the game's code still fails it.

## Verification

- Play tests by the owner on an M1 Pro with a gamepad: New Game through Diagon Alley, the Hogwarts arrival and the first missions,
  saves and loading; each bug above came from a play test or its recording and was reproduced before the fix.
- Scripted runs (`RECOMP_PAD_SCRIPT` with buttons and sticks, `HP1_START_LEVEL`, loading saves from the title menu) for every fix:
  the arrival skipped in about a second, Ollivanders' scene in 1.7 s, the camera turned by the right stick at both frame rates.
- `tools/smoke.sh`: 75 s, the menu opened and closed, the last frame not black: OK.
- `tools/make_dmg.sh`: DMG 5.2 MB, `tests/dmg_no_game_code.sh` PASS (94 disc files, 44 content windows of 3.1 MB of code).
- A headless install from the DMG into a clean HOME (`HP1_NO_UI=1 HP1_IMPORT_ISO=… HP1_INSTALL_DIR=…`): copy 1.8 GB + check +
  lift + compile 27 files + link + icon = **1 min 44 s** on the M1 Pro; the installed app started from its player folder
  (`game/`, `player/`, `saves/`, `cache/`, `logs/hp1.log`), bridged 110 of 110 kernel imports, played the opening cutscenes and
  exited cleanly on the smoke timer.
- `tools/publish_check.sh`: PASS.

## Pitfalls (each cost a run)

- **A seed inside an instruction** cuts a function in two; its switch cases become fake functions without their tables.
- **Widening a time hack** ("everything per frame 8x") broke a level transition that no test reached. Keep such changes to code
  a test reaches.
- **Sampling a video at 1 fps** hid which part of a scene was slow; 3 fps around the moment showed it.
- **zsh** reads `$t:shot` as a variable modifier: write `${t}` in generated scripts.
- **An apostrophe in the game's name** ("Sorcerer's") breaks single-quoted shell and AppleScript strings: the installer builds
  those with double quotes, and `osacompile` checks them.
- **Frame dumps at 4x are 11-20 MB each:** a long run of them filled the disk and later dumps came out black. Dump sparsely and
  delete them.
