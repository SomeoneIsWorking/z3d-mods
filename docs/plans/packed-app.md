# Packed app (harness as the product)

User: "I usually prefer packing since I can add hotswap and such", "And like game specific options
menu", and "We can also make the harness itself an app with a custom menu maybe instead of shipping
this as a libretro thing, we ship it as an app + mods, for potential licensing issues, we could add
some like mod compatibility layers so we don't ship licensed mods, instead we add support for them
but for my own installation I want the mods to be here".

## What it is
A windowed app grown from `harness/`: it loads the Azahar core from our fork, runs one game, and
owns a per-game options menu (cutscene skip, quick text, layout, texture set) with hotswap of our
own patches and of texture packs.

## What ships and what does not
- Ships: the app, our MIT patches (`patch/`), and loaders that apply third-party mod formats
  (`code.bps`/`code.ips` with exheader, LayeredFS romfs, Azahar texture packs, legacy and new hash).
- Does not ship: third-party mods (The One Screen, Project Restoration, Omni Xbox UI, Henriko
  textures) and no game data. The player points the app at their own copies; the loaders check each
  one against the hashes in its set.
- The user's own install keeps all current mods: the app reads the same sets `z3dmods install` uses.

## Licensing facts
- User: "I can make my own project GPL but I don't like having any restrictions". The app stays
  MIT, so it never links or loads Azahar's core in its own process: Azahar (GPL-2.0-or-later) runs
  as a separate program the app starts and drives over a local channel, shipped with its source.
  GPL mods (DekUI, Project Restoration) are evidence only; their code is never copied.
  Not shipping as a libretro core does not remove this.
- Third-party mods without a permissive license are never redistributed; the compatibility loaders
  avoid that by applying mods the player already has, not by bundling them.

## Open
- Hidden-window mode for tools (the current harness is already that).
- Input: real host events for the player; the tool path keeps the `input` command.
- The openSU agent is told when the app exists.
