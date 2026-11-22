# How engine/ was made

`engine/` is the `engine/` of the [Harry Potter and the Sorcerer's Stone port](https://github.com/sanyokkua/hp1-xbox-port) at its
v0.1.0, imported so that this port builds from this repository alone. That tree is itself the `engine/` of the
[NFS Underground 2 port](https://github.com/sanyokkua/nfsu2-xbox-port) with NFSU2's own parts removed (the Switch and iOS hosts,
the VP6 movie player, the HUD mover, the network card, the text patch, the texture and prompt packs) and HP1's work on top.
Everything this game needs on top of that is in this repository's git history: the lift and seeds, the XDK 5028 glue at this
game's addresses, the game's hand-written functions (`engine/src/recomp_manual.c`), the settings rows and the runtime and
renderer fixes (marked `hpcos-xbox-port` in the code). `engine/tools/patch_gen.py` patches the one lifted function that needs it.

The NFSU2 `engine/` was itself made from [myUdav4iik/nfsu2-ios](https://github.com/myUdav4iik/nfsu2-ios) at `85d2cbf` (branch
`ios`; nfsu2-sw v0.5.13 by antoxa2584x with the Apple host and the Metal renderer) with the patches in this folder, applied in
order (`git am`). They are kept here as the record of where the engine's code comes from:

- `0001`-`0024`: NFSU2Deadware's patches by LeoDeadware (MIT), without its HD texture loader (0005's `apple_textures.m` part,
  0010's `apple_textures.m` hunks) and its Ukrainian text and fonts (its 0024, 0025). Their numbers here differ from
  NFSU2Deadware's from 0024 on.
- `0025`-`0027`: the NFS Underground 2 port's (Settings window, player folder, F1).

The upstream agent notes (`CLAUDE.md` of nfsu2-ios) are in `engine/docs/NFSU2_IOS_AGENT_NOTES.md`.
