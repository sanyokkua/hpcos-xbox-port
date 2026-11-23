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
| Cutscene skip design | [Barthes-Recompiled/HP2-Recompiled](https://github.com/Barthes-Recompiled/HP2-Recompiled) (a fork of HP-COS, MIT) | |
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

## 3b. After the owner's first play test (2026-11-23)

The owner played about 10 minutes from a recording and a log. What was wrong, and the fixes:

1. **Windows flickered** (decals z-fighting their walls). The game draws its 3D with the W-buffer on (`SET_CONTROL0` bit 16,
   `Z_PERSPECTIVE_ENABLE`: control0 `0x00110001`), where the depth is w, not z/w. The renderer stored z/w, whose precision far
   off is too poor for a window a few centimetres in front of its wall. Now such draws write depth = w / zmax from the fragment
   shader (a perspective-correct varying, so exact; a pipeline variant, `RECOMP_MTL_WDEPTH=0` off). A burst of 11 frames 0.1 s
   apart over the intro's house: 3 flickered before, none after. HP-COS does the same with a log2(1 + w) depth.
2. **Shadows that slid with the camera, and a haze over some scenes**: the ambient occlusion (Settings > Graphics > Effects,
   "Bloom + Ambient Occlusion") linearised the depth with NFS's near and far planes. With W-depth its sky dome read as near,
   so the sky behind the trees darkened (the haze) and dark halos ran along walls. AO now reads the W-buffer as linear depth
   with w_far = 480 world units (`NFSU2_AO_WFAR`; measured: Harry 4 m away reads 0.0092, a debug probe
   `NFSU2_POST_PROBE=1`). The occlusion term now shows contact shadows only.
3. **A line through the title picture** (and one more at 80 %): the 640-wide picture is three tiles 256 wide with WRAP
   addressing that overlap by half a console pixel. Upscaled, the overlap's sub-pixels sample just past u = 1, blended with
   the tile's opposite (dark) edge. A screen quad (no depth test, 3-6 vertices) whose texture coordinates span [0, 1] samples
   stage 0 clamped to its edge (`RECOMP_MTL_QUADCLAMP=0` off; HP-COS holds them half a texel inside).
4. **Cutscenes were not skipped**: the hold-for-half-a-second design was missed in play (13 Start presses, none held). Now one
   press of Start or Back in a cutscene (camera mode `[0x50C2E4]` 7) starts it, and that press is hidden from the game. Speed
   (owner's second test: "FPS drops to 4 and it doesn't skip"): the vblank speed-up alone gave 1.3x, and drawing only every
   250 ms on the GPU 2.4x. A sample showed why: 93 % of the game thread in the world render `sub_00035190` waiting on the GPU
   (push-buffer and vertex-buffer space, `BlockOnTime`), and Swap `sub_00134D70` waits for the previous frame and the vblank.
   The frame loop (0x13170) is logic (`sub_00035110`), render, Swap; while skipping, the render's scene draw (its virtual
   call; its prep `sub_00038C80` and counters stay) and Swap are made only every 250 ms. A pass is then the logic alone:
   1,500-2,400 passes a second, the intro's remaining minute in about 2 s, the same state after it as without the skip. It
   stops at the first pass out of mode 7 (any grace would be seconds of fast gameplay) and goes on into a next part that
   starts within 2.5 s. The vblank and APU run 6x (10x+ starves the timer thread). HP2-Recompiled leaves out `sub_00032980`;
   its caller `sub_00032B70` loops until it returns 0.
5. **Controller**: LB / RB were Black / White; the Controller S has White on the left: LB is White, RB is Black now (xemu,
   HP-COS). "Presses ignored now and then": a press that begins and ends between two reads of the game (a slow frame, a load)
   was lost. The controller events now latch presses until the next read (`RECOMP_PAD_LATCH=0` off). The play-test log has
   timestamps on `[pad]` lines now, for the next report.
6. **A crash, 1 boot in ~6 under load**: D3D's DPC called 0xFFFFFF00. InsertCallback's pair went straight into PGRAPH
   0x1A88 / 0x186C (item 3 of section 3), and a later Z clear (0xFFFFFF00) overwrote it before the deferred DPC read it. The
   pair is now held by the executor and written when its NOP trap is raised, once the NV2A DPC is not queued or running.
7. **The load hang** (a stall at boot or a loading screen, the main thread polling a request whose callback never ran):
   the worker's wait for work (`sub_00114A10`, INFINITE) is now 50 ms waits (`HPCOS_FC_WAIT_MS`), so a lost wake-up costs
   50 ms instead of the game. That was not the whole cause: 1 stall in 36 boots (6 at once) remains, at boot while
   `egos.geo` opens (the hook of request 0xC00 ran, its callback 0x163E0 never did). Open.
   **"The disc is dirty"**: skipping the intro movie closes its file while the movie's reader thread still reads it; the read
   failed with STATUS_INVALID_HANDLE, a disc error to the game (seen 1 boot in 18 with the A press at 24 s). A read on a handle
   closed in the last 2 s now ends the file instead (STATUS_END_OF_FILE).
8. **Saves**: the game has no save menu. It saves by itself to the slot chosen at New Game (Slot A/B/C, "Continue" on the
   title); the "Save Game" text in the disc belongs to a debug menu ("Level Selector", "Cheats"). The Burrow has no save
   point; the save code is the same XAPI path as HP1's, where saving works.

## 3c. After the owner's second play test (2026-11-23)

1. **Saves did nothing.** A save scroll first tests the slot picked at New Game, `[0x51DF64]` (0x80EB0); at -1 ("started
   without the save option") it plays its animation and returns. New Game sets it (0x101CA5, after the free-space check
   `sub_00016D40`). The game systems' init `sub_0004ABF0` (from the game mode's init 0x34D00) sets it to -1; the owner's
   game had saving off in Borgin & Burkes, after the Burrow's End Day. In play that reset now keeps the slot
   (`HPCOS_KEEP_SLOT=0` off); every slot change is logged (`[save] slot`). Not yet confirmed at a scroll (a warp skips the
   story, and the scroll does not appear). Retail has no save menu: autosave-free, scrolls only; "Continue" on the title
   appears once a save exists (bit 0x10 of `[0x547030]`).
2. **The boot/load stall, found.** The loaders' file-cache callbacks (`[0x1BE8C8]` boot, `[0x436F2C]` level) published
   their next state, then released the request and stored its result (the file id). The release blocks on the cache's lock
   while the main thread holds it to poll; the main thread saw the new state, read the previous file's id and seeked a
   closed file (STATUS_INVALID_HANDLE, found by tracing the worker's list operations with each request's status). The 12
   callbacks of that shape now store first and publish last (`HPCOS_LOADER_ORDER=0` off). Boots: 0 stalls in 36 short and
   18 full (3 in 36 before). The handle table is also locked now.
3. **Moving shadows**: with the camera held still the ground's dark patches still drift: the game's own cloud shadows.
4. **Sticks**: no second dead zone on the left stick (the title has 0.35 and a run threshold); HP-COS's range compensation
   (a push to 65 % reads as full, `RECOMP_STICK_RANGE`). The right stick keeps the radial dead zone.
5. **Dev switches** for tests: `HPCOS_WARP=<level>[:start]` (the debug level selector's call; 4 Borgin & Burkes),
   `HPCOS_POS_LOG`, `HPCOS_TP="x,z;..."`, `HPCOS_PAUSE_OPTS` (the pause menu's debug rows), `HPCOS_SAVE_LOG`.

## 3d. After the owner's third play test (2026-11-23)

1. **Saves**: a SAVE GAME scroll ends in `sub_0004DD30` (copy the game state into the slot, open the save screen). The
   lifter had missed it (no direct caller, it follows data) and `recomp_stubs_unresolved.c` held an empty stub, so the
   scroll showed its tutorial and then did nothing. Seeded in `engine/config/seed_functions.json`. Tested by script:
   `HPCOS_SAVE_NOW=4` in the Burrow writes `game/UDATA/4541000e/<id>/POTTER A`; the next run's title shows Continue →
   "Potter A – The Burrow" and loads it. `mac/import_disc.py` keeps `UDATA`/`TDATA` across a new import.
2. **Stub audit**: of the 46 stubs, only 0x4DD30 was a missed function (`tools/seedcheck.py` says "free"); the others are
   reached from duplicate or misdecoded copies of functions.
3. **Short presses**: each press reaches the game for at least two reads (`RECOMP_PAD_MIN_READS`, default 2).
4. **Moving shadows**: not a texgen or AO bug as far as tests show (same picture with AO on and off); open.

## 4. Port features

- **Settings menu** (the NFS ports' overlay): this game's rows in `engine/src/overlay/`. Pages: Display, Graphics, Audio, Input,
  Tweaks, Advanced.
- **Skip Logo Movies** (the logo skip of HP-COS): off by default.
- **Skip Cutscenes** (the design of HP2-Recompiled): on by default. Press Start or Back in a cutscene and it runs to its end
  in a few seconds (about 30x), muted, the picture updated 4 times a second. Nothing is left out. See 3b, item 4.
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
- Boots: 35 of 36 to the intro cutscene with 6 at once before the end-of-file fix, the 36th a stall at boot (see 3b, 6 and 7). Audio: output dumps of the logos, the intro, the title menu and the
  Burrow are continuous, with no clicks or noise bursts beyond the sounds themselves.
- `tools/make_dmg.sh`: DMG 5.2 MB, `tests/dmg_no_game_code.sh` PASS (2,263 disc files, 64 code windows).
- A clean install from the DMG: **1 min 27 s** on the M1 Pro.

## Not verified, known limits

- **Saving and loading** are not tested. The game saves at story points; the tests did not reach one.
- **Anything past the Burrow** is not tested.
- **Widescreen:** 4:3 only. **PAL disc:** not supported.
- **Saving** is not confirmed yet (3c, item 1).
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
