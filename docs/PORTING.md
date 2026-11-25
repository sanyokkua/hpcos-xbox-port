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
   (since 3h: learned per direction, `RECOMP_STICK_RANGE=<fraction>` fixes it at the old 0.65 behaviour, 1 = off). The right stick keeps the radial dead zone.
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
4. **Moving shadows**: not a texgen or AO bug (same picture with AO on and off); found later, see 3f.

## 3e. After the owner's fourth play test (2026-11-23): one-sided Climb/Grab/Pull

1. **Differential test** (`tools/difftest.py`, needs `pip install unicorn` in `.venv`): `HPCOS_DIFF_DIR=<dir>
   HPCOS_DIFF_AT="n,..."` dumps registers, x87 state and guest memory around those calls of Harry's update
   (`sub_00053710`); the script runs the original bytes from the "pre" dump in Unicorn and compares with "post".
   `DIFF_WATCH=<va>` prints the instruction that writes an address, `DIFF_TRACE=<file>` the block path. Writes by other
   threads during the call show as "written by lifted" noise (thread stacks, timers).
2. **Bug**: Harry's heading from the stick (0x62C74 → `sub_00036E10` → the CRT's asin at 0x12767C) came out 90° off when
   the stick pointed straight along an axis: asin(±1) loads π/2 with `fld xword ptr [0x21336E]`, which the lifter read as
   a 4-byte float (≈ 0). Fixed in the lifter (`fld`/`fstp` m80 → `recomp_ld_f80` / `recomp_st_f80` in `recomp_types.h`;
   19 loads, 4 stores in this game) with a unit test; after the relift every capture matches.
3. Audit: the four game functions replaced outright are platform ones (memmove, the AC'97 reset, rumble, the cache
   worker's wait); every other override calls the original. No x87/SSE instruction is left as a TODO.


## 3f. After the owner's fifth play test (2026-11-23): sliding shadows

Controls confirmed. The "moving shadows" were the character shadow map's border. Each frame the game renders the
characters' shadows into a 1024x1024 R5G6B5 target (836D4000 in Diagon Alley), cleared to grey, and every world draw
samples it on stage 1 (projected, address mode 4 = border) with border colour `FF808080`. Metal has only transparent
black, opaque black and opaque white borders; the old code picked white, so outside the map the world was lit at double
brightness and the map's edges were dark bands that moved with Harry and the camera. Now `wrap_mode(4)` clamps to the
edge and the fragment shader's `nv2a_samp()` returns the stage's `SET_TEXTURE_BORDER_COLOR` (`Nv2aMslFs.bc/bm`) when a
coordinate is outside [0,1]. Found with `RECOMP_DRAW_LOG` (now prints `bc`) and `RECOMP_GL_DUMP_SURF=836D4000`; checked by
painting the border red (the far floor through the arch) and by before/after dumps. Test with the owner's save:
`build/t/gr1` (POTTER B, Continue → down → A).

## 3g. After the owner's sixth play test (2026-11-24): the freeze after a door

**Symptom.** In the Leaky Cauldron cellar (and once in Knockturn Alley) the game crashed or froze for good right after Harry
opened a door, a few seconds after he had picked up Bertie Bott's beans or coins. The log showed `[ICALL]` wild calls from the
entity update and then the NOP trap repeating.

**Reproduction.** `HPCOS_WARP=8` (the Leaky Cauldron) from the owner's save, `HPCOS_PAD_REPLAY=<log>,<t0>,0` with
`HPCOS_REPLAY_AT_WARP=1` (the owner's own pad input, its clock started when the warped level is in play) and `HPCOS_FF_AUTO=1`
(every cutscene skipped as it starts, as the owner did): 3 of 3 froze at the door.

**Cause: an empty stub on real code.** `engine/gen/recomp_stubs_unresolved.c` gives every call or jump target the disassembler
did not see as a function an empty body (`g_esp += 4`). The bean and coin pick-up (`0x721C0`) ends with a tail jump to a shared
exit at `0x72510` (`pop esi; pop ebp; pop edi; ret`) that no function owned, so it got such a stub. The three pops never ran: the
entity update (`sub_00042F70`) got its stack back 12 bytes short and its loop registers replaced by pointers, walked off the
entity list and called through `[garbage+0x74]`. The improved wild-call log (the guest return address, the last 16 indirect
call targets, the pool slot the target falls in) pointed at the pick-up every time.

**Fix.** `tools/stubcheck.py` classes every stub: an instruction start inside a detected function (`BOUNDARY`) or a stub in
`.text` outside every function (`code`) is real code; inside an instruction (`mid-insn`) or outside `.text` (`data`) it is a
misdecode and harmless. 33 of 45 stubs were real code. 31 of them are in HP-COS's function list. Seeding a stub can split a
function and turn its jumps into new stubs, so it took three rounds: 47 seeds, now 13 stubs and 0 real.
`tools/relift.sh` runs the check after every lift and fails when one is real code. After the fix: 3 of 3 through the door,
8 of 8 boots to play, a save written, and the owner played 46 minutes from Diagon Alley to Hogwarts with no wild call.

**The audit that came with it** (the owner asked for every similar pattern to be checked):
1. A wild call that is skipped keeps the stack balanced: `recomp_icall_skip_esp()` reads the return site, and when it is
   `add esp, n` (a cdecl call) it pops the return address only.
2. File handles: a handle count apart from the reference count, `CloseHandle` drops one handle once, a closed object is never
   found again, `WaitForSingleObject` and `NtReadFile`/`NtWriteFile` hold a reference while they use one. Reads and writes at an
   offset are `pread`/`pwrite` (atomic).
3. Timers: firing signals inside the timer lock before the DPC, and `KeSetTimer` resets inside it, so a wake-up is never lost.
   256 timers, 65,536 handles, a log when either runs out.
4. Kernel objects: a stale host shadow is dropped when the guest re-initialises the header as another type; Ke*Initialize*
   writes the header before the shadow.
5. The heap refuses a size larger than itself (a 64-bit top check).
6. The crate spawner's clean-up skips entities that are no longer its own (HP-COS's guard, `sub_000FC830`).

**The release check found one more** (a clean install, 2026-11-24): the installed app aborted in `SDL_NumJoysticks`
(`pointer being freed was not allocated`), called from `xbox_InputGetState` -> `xbox_InputInit` on the game's thread, right as
the controller disconnected. sdl2-compat refreshes one global pad list, without a lock, both there and while it turns an add
or remove into an event on the main thread. Now `xbox_input_rescan()` opens and closes the pads on the host's main thread (at
start and on every add or remove); the game's thread only reads them, under a lock, and no longer pumps SDL. Stress test:
`NFSU2_PAD_HOTPLUG_TEST=<ms>` attaches and detaches a virtual pad that often (37 ms: 1,600 cycles in 2 minutes, no crash).

## 3h. Backports from the Prisoner of Azkaban port (2026-11-24, Unreleased)

Three fixes hppoa-xbox-port found (its `7b59edc`, `c053b83`, `79c7af5`); none changes this game's lift or smoke result.

1. **Disassembler** `_prune_data_call_targets`: calls made by functions that are themselves call targets called only from data no longer
   count as a real caller, so they cannot keep a start that splits an instruction. Two tests; the 62 disasm tests pass. Relift: `functions.json`
   before and after identical (6,131 starts, no start dropped or added, no end moved), so nothing to explain here.
2. **Kernel** `XC_FACTORY_AV_REGION` (0x103) = 0x00400100 (NTSC-M). This port does not pick a region: it fixes `XC_DVD_REGION` 1 and
   `XC_FACTORY_GAME_REGION` 1 (USA disc only), so the fixed NTSC-M value is consistent. Only matters if the game calls `XGetVideoStandard`.
3. **`tools/stubcheck.py`**: range `0x11000..0x17DA80` (`.text` plus D3D, D3DX, XGRPH, DSOUND, XMV, XPP), bytes past a section's raw data
   are data. Before, the stubs in the XDK sections were classed "data" without a look. Result: 13 stubs, 0 real code, nothing seeded.
   Smoke (`tools/smoke.sh 60`): OK before (4 frames, mean 77) and after (4 frames, mean 68).

### 3h, round 2 (Unreleased)

4. **Metal shaders as MSL 3.0** (`nv2a_metal.m`; `RECOMP_MTL_LANG=<maj>.<min>`, 0 = Metal's default). Log: `[MTL] shader language 3.0`, no
   `shader failed` or `pipeline creation failed` line. hppoa measured a combiner shader at 540 ms (default) vs 43 ms (3.0). Not reproduced
   here: the smoke route (`tools/smoke.sh 90`, fresh HOME, 2 runs each, default here is MSL 4.0) has the same hitches with both (a 350-520 ms one
   at frame ~1247 with 0 ms of pipelines, so not a shader compile) because Metal's compile cache is warm after the first run; only the very
   first 3.0 run showed 12 programs / 22 pipelines in one 617 ms frame (233 ms pipelines). The hitch log now also counts pipelines. A cold-cache
   play route is still needed to confirm the gain.
5. **`tools/immscan.py`** (from hppoa, plus the XMV section this XBE has): 310 immediates into code, 0 missed functions, as before.
6. **Learned stick range** (`stick_deadzone.h`): 8 directions each keep the furthest reach seen (at least 0.60, at most 1), radial scaling by it.
   Tests in `engine/xboxrecomp/tests/stick_deadzone` (all pass). The executor's inline fixed-function change is not taken (still being checked in PoA).

### 3h, round 3: the pushbuffer executor (Unreleased)

7. **hppoa's executor fix** (its `006d8a3`, three parts): inline and SET_VERTEX batches in fixed-function mode are transformed
   (`RECOMP_INLINE_FF=0` = the old rule), `SET_VERTEX_DATA4F(3)` diffuse is used, and 1-vertex point and 2-vertex line batches are kept.
   Not taken: the round-2 debug aids and the overlay `capture` action.
   **Safety check** (temporary probes, not committed): routes = logos, intro, title, New Game to the Burrow (HUD, pause), and
   `HPCOS_WARP` 4, 8, 12, 20 (Borgin & Burkes, Leaky Cauldron, Whomping Willow, ...), with START pulses and FF_AUTO. Result:
   - **Inline/SET_VERTEX batches in fixed-function mode: 0 draws** in every route, and **SET_VERTEX_DATA4F(3) diffuse: 0**. For these
     two parts the new and the `RECOMP_INLINE_FF=0` binaries run the same code on this game's routes. Screenshot pairs of the
     two runs (logos, title identical to the pixel; Burrow, HUD, pause differ only where the game state differs: runs are not
     frame-locked, pause/scene timing and lighting flicker move) are therefore no evidence either way beyond that.
   - **Short batches: only in the Whomping Willow level (12)**, ~280 per frame, all 2-vertex `LINES` through a vertex program.
     Controlled A/B in one process (lines dropped on alternate frames, `RECOMP_GL_DUMP_AT` 16 consecutive frames): same-parity
     frames differ by 8 px, alternating ones by ~2,370 px, and the difference is exactly thin slanted streaks over the whole
     screen: the rain. A fix (the rain was missing), nothing else changes.
8. Smoke (`tools/smoke.sh 60`): OK.

### 3h, round 4: shared Metal renderer fixes from hppoa (Unreleased)

9. **P8 textures hashed in full** (`bytes_hash_all`, hppoa `b010ad5`), **polygon offset slope x render-target px / game px**
   (`RECOMP_MTL_ZSLOPE=0` off) and **reflection / normal / sphere map texgen** (0x8512, 0x8511, 0x2402; `RECOMP_MTL_TEXGEN_MAPS=0` off)
   (hppoa `918dac2`). Not taken: its skin second-layer matcher (`RECOMP_SKIN_LAYER`, PoA only).
   **Check** (Burrow route and `HPCOS_WARP` 4, 8, 12, 20, scale 300, 80 s each; baseline, all fixes on, `RECOMP_MTL_TEXGEN_MAPS=0`,
   `RECOMP_MTL_ZSLOPE=0`): none of the three code paths is reached on these routes, so nothing visible changes:
   - P8: `RECOMP_TEX_CHANGES=1` lists only format 06 changes (9,696 lines over the runs), no format 0B, before or after.
   - Texgen: no `texgen ... not generated` line at baseline (that line is what any of the three modes printed), none after; the new code
     only acts on those modes.
   - Polygon offset: no `polygon offset in use` line before or after (the title never enables it on these routes).
   Screenshots of on/off runs differ by as much as any two runs (they are not frame-locked); static scenes (w8, 2-9 k px) match.
   No crash, `tools/smoke.sh 60` OK. A scene that uses these (a reflective or env-mapped surface, a polygon-offset decal) is untested.

### 3h, round 5: NV2A renderer fixes from hppoa's xemu audit (Unreleased)

10. **Five fixes** (hppoa `0961689`, patch hand-merged; each has a switch): depth clamped per pixel (`nv2a_clip`;
    `RECOMP_MTL_ZCLAMP_PIXEL=0` old per-vertex clamp, `=0ab` alternates every 2 s), clip-range pixel cull in CULL mode
    (`RECOMP_MTL_ZCULL=0`; **skipped for W-buffer draws** via `key.wdepth`), stencil op 0 = ZERO (`RECOMP_MTL_STENCIL_ZERO=0`),
    alpha test on rounded 8-bit alpha (`gl_psh.c` is shared with Metal; `RECOMP_ATEST_ROUND=0`), no FF normal flip toward the eye
    (`RECOMP_FF_FLIP_NORMALS=1` restores it). Not taken: four dev-log hunks (`RECOMP_MTL_ZCLAMP_LOG`, `RECOMP_DL_ATTRS`,
    `[dl]` clip/stencil fields, per-vertex screen position) that did not apply; the two debug switches (`RECOMP_MTL_SKIP_TEX`,
    `RECOMP_GL_DUMP_SURF`) came with the patch.
    **Check** (temporary counters and a per-frame parity switch, not committed): route = `HPCOS_WARP=<level>:0 HPCOS_FF_AUTO=1
    RECOMP_PAD_PRESS=0x1010,2500,200` (without the pad press the game stays in the movies: no 3D draws at all, which also means the
    rounds 2-4 routes reached little 3D). In 3D (levels 12 and 4, ~5,300 frames): **CoS uses the W-buffer for ~88 % of draws**
    (3.1 M of 3.6 M), so the cull applies to the other ~12 % (~420 k draws); **stencil op 0: 0 uses; FF lighting normals: 0 calls**
    (CoS lights with vertex programs), so those two fixes change nothing here. Controlled A/B in one process (clamp or cull
    alternating per frame, `RECOMP_GL_DUMP_AT` 6 consecutive frames at 60 s and 85 s): alternating frames differ no more than
    same-parity ones (~0.7-4 k px of animation; the larger steps are scene changes, not parity). No visible change, no regression.
    The alpha rounding is a shader-text change (cannot alternate); it is untested beyond smoke. `tools/smoke.sh 60` OK.

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

Dev scripts: `tools/stubcheck.py` (is any empty stub real code; `tools/relift.sh` runs it), `tools/run_test.sh <name> <secs> <shot times>` (a scripted run with screenshots), `tools/sheet.sh` (a contact sheet
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
| `HPCOS_WARP=<level>[:start]` | load a level 3 s into play, as the pause menu's debug level selector does (the level numbers are HP-COS's trainer's) |
| `HPCOS_SAVE_NOW=<secs>` | ask for a save once, as a SAVE GAME scroll does |
| `HPCOS_PAD_REPLAY=<log>,<t0>,<offset>` | replay the pad reports of a play log (`HPCOS_PAD_LOG=1`) |
| `HPCOS_REPLAY_AT_WARP=1` | start the replay's clock once the warped level is in play |
| `HPCOS_FF_AUTO=1` | skip every cutscene as it starts |

## Verification

- Scripted runs on an M1 Pro: the logos and the intro movie, the title, New Game and the save-slot screen, loading, the Burrow
  intro cutscene with subtitles and speech, free play at the Burrow (Harry walks and casts, the HUD, the Flipendo tutorial,
  Fred, George and Ron), the pause menu and the game's Options, 60 fps at 3x, sound.
- Boots: 35 of 36 to the intro cutscene with 6 at once before the end-of-file fix, the 36th a stall at boot (see 3b, 6 and 7). Audio: output dumps of the logos, the intro, the title menu and the
  Burrow are continuous, with no clicks or noise bursts beyond the sounds themselves.
- The owner's play tests: the Burrow, Knockturn Alley and Borgin & Burkes, Diagon Alley (Flourish & Blotts, the Leaky
  Cauldron, the Magical Menagerie), the Whomping Willow, Hogwarts at night and the first day; saving with a scroll and
  Continue from the title. The last test: 46 minutes, no wild call, no crash.
- Screenshots of Diagon Alley, the Leaky Cauldron, Hogwarts, the Grand Staircase and the dormitory: `HPCOS_WARP=<level>[:start]`
  from a save, `tools/run_test.sh`, 0 wild calls in each.
- `tools/make_dmg.sh`: DMG 5.2 MB, `tests/dmg_no_game_code.sh` PASS (2,263 disc files, 64 code windows).
- A clean install from the DMG: **1 min 27 s** on the M1 Pro.

## Not verified, known limits

- **The rest of the school year** (after the first day at Hogwarts) has not been played on the Mac.
- **Widescreen:** 4:3 only. **PAL disc:** not supported.
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
- **An empty stub is silent.** The lifter's "not detected" stubs return at once. On a shared exit that another function jumps to,
  the caller's registers and stack come back wrong and the game breaks much later, somewhere else. Run `tools/stubcheck.py`
  after every lift, and repeat after seeding (a seed can split a function).
- **A warp without a start point** (`HPCOS_WARP=30`) can leave Harry outside every room: only objects are drawn. Give one
  (`HPCOS_WARP=30:0`).
- **SDL from two threads.** sdl2-compat's pad list (`SDL_NumJoysticks`) has no lock. Only the thread that pumps SDL's events
  may open, close or enumerate pads; other threads read open ones.
