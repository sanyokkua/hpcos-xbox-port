# How engine/ was made

`engine/` is the `engine/` of the [NFS Underground 2 port](https://github.com/sanyokkua/nfsu2-xbox-port) at `d23c105` (its
v0.1.0), imported on 2026-11-20 so that this port builds from this repository alone, with NFSU2's own parts removed (the
Switch and iOS hosts, the VP6 movie player, the HUD mover, the network card, the text patch, the texture and prompt packs).
Everything HP1 needs on top of it is in this repository's git history from its first commit (`aed9776`): the HP1 lift and
seeds, the XDK glue at HP1's addresses, the game's hand-written functions (`engine/src/recomp_manual.c`), the settings rows
(`engine/src/overlay/hp1_options.c`) and the runtime and renderer fixes (marked `hp1-xbox-port` in the code).

That `engine/` was itself made from [myUdav4iik/nfsu2-ios](https://github.com/myUdav4iik/nfsu2-ios) at `85d2cbf` (branch
`ios`; nfsu2-sw v0.5.13 by antoxa2584x with the Apple host and the Metal renderer) with the patches in this folder, applied in
order (`git am`). They are kept here as the record of where the engine's code comes from:

- `0001`-`0024`: NFSU2Deadware's patches by LeoDeadware (MIT), without its HD texture loader (0005's `apple_textures.m` part,
  0010's `apple_textures.m` hunks) and its Ukrainian text and fonts (its 0024, 0025). Their numbers here differ from
  NFSU2Deadware's from 0024 on.
- `0025`-`0027`: the NFS Underground 2 port's (Settings window, player folder, F1).

The upstream agent notes (`CLAUDE.md` of nfsu2-ios) are in `engine/docs/NFSU2_IOS_AGENT_NOTES.md`.
