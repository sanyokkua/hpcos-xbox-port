# How the port was made

A log of the steps, the decisions and what went wrong. It follows the Harry Potter and the Sorcerer's Stone port
([hp1-xbox-port](https://github.com/sanyokkua/hp1-xbox-port) v0.1.0) and the NFS Underground ports
([nfsu2-xbox-port](https://github.com/sanyokkua/nfsu2-xbox-port), [nfsu-xbox-port](https://github.com/sanyokkua/nfsu-xbox-port)),
which were made first with the same engine; this file says where Chamber of Secrets differs. The game is by Eurocom (2002), not
Warthog's engine of HP1 (2003): only the platform stack (Xbox kernel, D3D8, NV2A, DSOUND) is shared.

## Sources (pinned)

| What | Where | Pin |
|---|---|---|
| Engine, overlay menu, installer | [sanyokkua/hp1-xbox-port](https://github.com/sanyokkua/hp1-xbox-port), the NFS Underground 2 port's engine (xboxrecomp, nfsu2-sw, nfsu2-ios, NFSU2Deadware's patches) | v0.1.0 |
| This game's facts | [MiddleDucky/HP-COS-Xbox-Recomp](https://github.com/MiddleDucky/HP-COS-Xbox-Recomp) (MIT, Windows recomp of the same disc, same NTSC addresses) | `f45cb33` (2026-11-18) |
| Cutscene fast-forward design | [Barthes-Recompiled/HP2-Recompiled](https://github.com/Barthes-Recompiled/HP2-Recompiled) (a fork of HP-COS, MIT) | |
| Controller design | [MiddleDucky/HP-PS-Xbox-Recomp](https://github.com/MiddleDucky/HP-PS-Xbox-Recomp) (MIT) | |
| Disc | Xbox USA, `default.xbe` 2,174,976 bytes, SHA-1 `dd881f2c786464046f705bbe21f222e847d970ce`, title `0x4541000E`, XDK 5028, entry `0x10E698`, `.text` 1,177,120 bytes | section digests in `mac/xbe_check.py` |

HP-COS-Xbox-Recomp gave 177 indirect-call seeds, the XInput function addresses, the video-overlay design, the logo skip and the
lesson on the file-cache race. Its addresses are for the same NTSC executable, so they were used as they are.

## 1. The disc

- An XISO, 2,835 entries, about 2.5 GB. `default.xbe`, Eurocom engine files (`*.geo`, `*.gdt`, `*.sdt`, `*.snd`,
  `EAMusic/*.mus` and `*.mpf`, `langtext/*.bin`, `xbfat.fat`) and XMV movies (`ea_logo_*.xmv`, `warner.xmv`, the `hpengx.xmv`
  intro).
- The executable's `.text` matches its own stored digest: the image is pristine. (The NFS and HP1 discs were media-patched.)
  The check still compares the title, each section's size and stored digest (`mac/xbe_check.py`).

## 2. Lift, build, the XDK glue

- **Lift:** the engine's `regen.sh` with this executable. The XDK glue it needs is in section 3.
- **NFSU2-only parts** were already removed in the HP1 tree.
- **One patch after every lift:** `sub_000C0001` starts with a branch on its caller's flags. `engine/tools/patch_gen.py` fixes it
  (from HP-COS; this lifter already handles HP-COS's other three cases).

## 3. Bring-up, in the order the bugs appeared

Each was a hang or a black screen.

1. **XDK 5028 glue.** Found by masked byte signatures from HP1 and NFSU1 (`tools/sigmatch.py`): memmove `0x127A00`,
   `D3D_BlockOnTime` `0x1359D0` (by its prologue pattern), the device pointer `0x140888`, the vblank ISR `0x139080`, the PGRAPH
   handler `0x139690`, the DPC `0x1398D0` (seeded: only the DPC object reaches it), the DSOUND DSP post `0x1465D2`, the AC'97
   reset `0x14CBAD` (its internals remapped).
2. **99 "unresolved call target" no-op stubs.** 50 were real functions the scan missed (after nop padding or tail jumps).
   `0x113670`, the file request's finish, was one: reads never completed. Seeded (`tools/seedcheck.py`, `tools/addseed.py`).
3. **`InsertCallback`.** XDK 5028 passes a callback pointer and argument through `SET_ZSTENCIL_CLEAR_VALUE` and
   `SET_COLOR_CLEAR_VALUE`, which the chip latches into PGRAPH `0x1A88` and `0x186C`. The trap called a null pointer and the
   interrupts stayed masked. The executor now latches them.
4. **Movies.** They use the NV2A video overlay (PVIDEO), which nothing scans out. `UpdateOverlay` and `EnableOverlay` are
   replaced: frames are converted from YUY2 to RGB and drawn by a new movie layer in the Metal backend, which the executor
   thread presents again while the title flips nothing.
5. **All 3D vertices in one pixel.** `SET_VIEWPORT_SCALE` and `SET_VIEWPORT_OFFSET` also load transform constants 58 and 59 on
   the chip, and XDK 5028 relies on it (newer XDKs wrote the constants).
6. **Fog.** A fog unit (mode, generation mode, parameters) added to the Metal vertex shaders. The NFS games never used fog.
7. **The yard's ground vanished.** A render target sampled through a no-alpha format (R5G6B5) must read alpha 1: an opaque
   texture view. 16-bit clear colours are now decoded by the surface format.
8. **The load hang, about 1 boot in 8.** The file cache worker (thread `0x1146C0`) can reap a finished request before the game
   thread hooks its callback (`sub_00112E20`). On the console the DVD read always took longer than that. Now the worker waits at
   the top of its loop while a game thread is between allocating a request (`sub_00113090`) and hooking it, 200 ms at most
   (`HPCOS_CACHE_HOLD=0` turns it off). `NtSetEvent` also hands the guest lock to the thread it wakes. First version held
   for every thread: the music streamer queues reads it only polls, and the title-menu music played 0.25 s of every second.
   Now only the main thread, and its next file-cache call (another request, a status poll) ends the hold
   (`HPCOS_CACHE_RELEASE=0` keeps it to the hook). Rate: about 1 hang in 30 boots with 6 at once (about 4 in 30 with no hold).
12. **Movie sound screeched and clicked** (owner report): the EA logo and the intro have 16-bit PCM sound (the WB logo, Xbox
    ADPCM, was fine). Their packets sit in a heap buffer; `MmGetPhysicalAddress` returned the heap VA unchanged, which falls
    inside the contiguous arena's physical range, so the APU read the contiguous window's bytes (short noise bursts, the music
    missing). On the console both views are the same RAM; here they are two memories. For DirectSound's callers
    (`xbox_SetDsoundRange`, the DSOUND section) a heap address now comes back with bit 26 set and `apu_phys` reads it as a plain
    VA (`HPCOS_DS_TAG=0` off). Found by dumping the output PCM (`RECOMP_AUDIO_DUMP`), classifying 10 ms windows (high-frequency
    to RMS ratio), then logging each stream segment's samples at both addresses (`RECOMP_APU_VOICE_LOG`).
9. **Characters invisible.** Bone matrices lack the screen offset that static matrices bake in, so the skinning program adds
   `c59`, which only `SET_VIEWPORT_OFFSET` filled.
10. **24 fps.** The character-shadow draws render into a 1024x1024 page that stays bound (unread) on stage 1. Each draw copied
    the 4x page and split the render pass: 132 splits, 6.8 GB of tile traffic per frame. There is no copy now for a stage the
    combiners never read: 60 fps.
11. **The patched function:** see section 2 (`patch_gen.py`).

## 4. Port features

- **Settings menu** (the NFS ports' overlay): this game's rows in `engine/src/overlay/`. Pages: Display, Graphics, Audio, Input,
  Tweaks, Advanced.
- **Skip Logo Movies** (the logo skip of HP-COS): off by default.
- **Cutscene Fast-Forward** (the design of HP2-Recompiled): on by default. Hold Start or Back for half a second in a cutscene
  and it plays to its end about 8x as fast, muted. Nothing is left out.
- **Controller:** XInput functions at the addresses from HP-COS, with the direct design of HP-PS-Xbox-Recomp, fed from the
  runtime's pad report (so the keyboard, scripted input and the menu's neutral pad still apply). A radial stick dead zone
  (`engine/xboxrecomp/src/input/stick_deadzone.h`).

## 5. A self-contained repository and the installer

As in the other ports: `engine/` holds everything, `tools/kit.py` collects the lifter and the compiled runtime (no game code)
into the installer, `mac/build_game.sh` lifts, compiles and links on the player's Mac, `tests/dmg_no_game_code.sh` checks the DMG.
Here: the target `hpcos_recomp`, the binary `hpcos`, the `HPCOS_` installer variables (`HPCOS_NO_UI`, `HPCOS_IMPORT_ISO`,
`HPCOS_INSTALL_DIR`, `HPCOS_BUILD_JOBS`), the disc check, and the test's game file patterns.

## Tools

Dev scripts: `tools/run_test.sh <name> <secs> <shot times>` (a scripted run with screenshots), `tools/sheet.sh` (a contact sheet
of the shots), `tools/seedcheck.py` (is every seed an instruction start), `tools/addseed.py`, `tools/relift.sh`,
`tools/play_dev.sh` (play in a window, own HOME).

Debug switches added for this game:

| Switch | What |
|---|---|
| `RECOMP_IRQ_LOG` | interrupts |
| `HPCOS_TRAP_LOG` | the chip's traps and callbacks |
| `RECOMP_EVT_TRACE` | kernel events |
| `HPCOS_FC_TRACE` | the file cache |
| `RECOMP_VS_DUMP=<dir>` | vertex programs |
| `RECOMP_DL_CONST=1\|2` | transform constants in the command lists |
| `RECOMP_MTL_NODEPTH`, `RECOMP_MTL_NOATEST` | depth and alpha test off, to find what hides a draw |
| `RECOMP_SPLIT_WHO` | what splits a render pass |
| `RECOMP_VPOFF_LOG` | viewport offset writes |

## Verification

- Scripted runs on an M1 Pro: the logos and the intro movie, the title, New Game and the save-slot screen, loading, the Burrow
  intro cutscene with subtitles and speech, free play at the Burrow (Harry walks and casts, the HUD, the Flipendo tutorial,
  Fred, George and Ron), the pause menu and the game's Options, 60 fps at 3x, sound.
- Boots: about 1 load hang in 30 with 6 at once (see 8). Audio: output dumps of the logos, the intro, the title menu and the
  Burrow are continuous, with no clicks or noise bursts beyond the sounds themselves.
- `tools/make_dmg.sh`: DMG 5.2 MB, `tests/dmg_no_game_code.sh` PASS (2,263 disc files, 64 code windows).
- A clean install from the DMG: **1 min 27 s** on the M1 Pro.

## Not verified, known limits

- **Saving and loading** are not tested. The game saves at story points; the tests did not reach one.
- **Anything past the Burrow** is not tested.
- **Widescreen:** 4:3 only. **PAL disc:** not supported.
- **"Disc is dirty" screen:** seen once in about 70 boots. An invalid-handle read on the music stream; under investigation.
- **The file-cache hold** makes the old 1-in-8 load hang rare, but it works around a timing difference between the console and
  this runtime. It is not a proven cure.

## Pitfalls (each cost a run)

- **A seed inside an instruction** cuts a function in two; its switch cases become fake functions without their tables. Run
  `tools/seedcheck.py`.
- **The console's timing is a hidden contract.** The file-cache race never showed on the Xbox because the DVD was slow.
- **XDK versions differ** in what the driver writes: XDK 5028 leaves transform constants to the chip. Do not assume a newer XDK's
  behaviour.
- **Sampling a video at 1 fps** hides which part of a scene is slow; use 3 fps around the moment.
- **zsh** reads `$t:shot` as a variable modifier: write `${t}` in generated scripts.
- **Frame dumps at 4x are 11-20 MB each:** dump sparsely and delete them.
