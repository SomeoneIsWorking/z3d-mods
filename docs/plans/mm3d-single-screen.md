# Our own single screen (MM3D; OoT3D as fallback)

User: "single screen exists"; on the CC BY-NC-ND and fork routes: "I'd rather make MIT versions of
those then".

## MM3D
User, 2026-11-13: "Can't you copy the RE? Anyway, do what you can please", then on building
DekUI: "Look, no, no Qt".

DekUI (GPL-2.0) is not usable: it is its own Azahar fork whose single screen is host code in the
emulator (`src/mm3d/`, native inventory, file select, notebook and minimap drawn by the frontend),
and it builds only with the Qt or Android frontend. Its source (v1.1.0, `5cf063e`, checked out
read-only at `<dekui-checkout>`) is evidence for which guest functions and state each menu uses;
its code is not copied.

User, 2026-11-13: "this needs to be an app using Azahar libretro underneath": single screen moves
to the packed app (docs/plans/packed-app.md), drawn by the app. The code-patch route below is the
fallback for plain Azahar.

Fallback: an MIT code patch that moves the bottom-screen UI (items, masks, map, notebook, Ocarina,
file select) onto the top screen in the game's own code, in the same `code.ips` as the skip, on
Project Restoration's v1.0-layout `code.bin`, built from mm3d-decomp and PR's source (GPL, evidence
only). It does not fork Azahar.

## OoT3D (fallback only)
If The One Screen cannot be used, the same approach for OoT3D, built from the OoT3D Randomizer's
loader (MIT) and zeldaret/oot3d (CC0), in the same `code.ips` as the skip and Quick Text.
