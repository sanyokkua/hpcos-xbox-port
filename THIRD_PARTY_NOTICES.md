# Third-party notices

Harry Potter Sorcerer's Stone for Mac (GPL-3.0-or-later, `LICENSE`) is built on the work below. The installer ships each one's
license text in `Contents/Resources/kit/licenses/`.

| Project | What it gives | Where | License |
|---|---|---|---|
| [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) by antoxa2584x | the engine around xboxrecomp: lifter fixes, kernel and D3D glue, the host the game runs in | `engine/` (by way of nfsu2-ios and the NFS Underground 2 port) | no license file upstream; used here under this project's GPL-3.0-or-later, with credit (see `NOTICE`) |
| [nfsu2-ios](https://github.com/myUdav4iik/nfsu2-ios) by myUdav4iik | the Apple host and the Metal renderer (`nv2a_metal`) | `engine/` (from `85d2cbf`), the kit's objects | no license file upstream; as above |
| [HP-PS-Xbox-Recomp](https://github.com/MiddleDucky/HP-PS-Xbox-Recomp) by MiddleDucky | this game's facts (addresses, the controller, the save signing, the logo movies, functions behind jump tables); the controller and logo-skip code | `engine/src/recomp_manual.c` (marked where it follows it) | MIT |
| [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nz | the Xbox lifter (Python) and runtime: kernel, D3D8, NV2A, audio, input | the kit's `lift/` and libraries | MIT |
| [xemu](https://github.com/xemu-project/xemu) | the APU (MCPX audio) and NV2A parts inside xboxrecomp | the kit's libraries (relinked on the player's Mac) | LGPL-2.1-or-later |
| [NFSU2Deadware](https://github.com/LeoDeadware/NFSU2Deadware) by LeoDeadware | the Mac engine patches (render scale, Metal effects, mip chains, pacing), the kit builder, disc import, icon | `engine/` (via the NFS Underground 2 port), `tools/kit.py`, `mac/` | MIT |
| [reUndr-ps5](https://github.com/bystritskiy/reUndr-ps5) by bystritskiy | the idea of the disc check by section digests | `mac/xbe_check.py` (written for these ports) | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9 by Omar Cornut | the settings menu's drawing, Metal backend | `engine/third_party/imgui`, the kit's `libimgui.a` | MIT |
| [Roboto](https://fonts.google.com/specimen/Roboto) Medium | the settings menu's font, compiled in | `engine/third_party/fonts` | Apache-2.0 |
| [SpeedBreaker](https://github.com/SpeedBreakerProject) by project(u) | the settings menu's design (pages, rows, controller first, Quit Game) | `engine/src/overlay` (written for the NFS Underground 2 port) | GPL-3.0-or-later |
| [Capstone](https://github.com/capstone-engine/capstone) | the x86 disassembler the lifter uses | the kit's `py/capstone` | BSD-3-Clause |
| [SDL3](https://github.com/libsdl-org/SDL) 3.4.16 | window, input, gamepads, sound | `libSDL3.dylib` | Zlib |
| [sdl2-compat](https://github.com/libsdl-org/sdl2-compat) 2.32.74 | SDL2's API on SDL3 (the runtime is written against SDL2) | `libSDL2-2.0.0.dylib` | Zlib |
