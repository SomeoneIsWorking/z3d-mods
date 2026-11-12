# Our own single screen (MM3D; OoT3D as fallback)

User: "single screen exists"; on the CC BY-NC-ND and fork routes: "I'd rather make MIT versions of
those then".

## MM3D
Now: DekUI on the USA 1.1 dump the player will supply. Later, after the skip: this MIT variant.

The existing single screens (DekUI, Majora ReMasked) are GPL-2 forks of Azahar and need the USA
1.1 dump; the player has 1.0. Ours is an MIT code patch for MM3D 1.0 that moves the bottom-screen
UI (items, masks, map, notebook, Ocarina, file select) onto the top screen in the game's own code.
It rides on Project Restoration's baked `code.bin` as part of our `code.ips`, built from the player's
mm3d-decomp. It does not fork Azahar.

## OoT3D (fallback only)
If The One Screen cannot be used, the same approach for OoT3D, built from the OoT3D Randomizer's
loader (MIT) and zeldaret/oot3d (CC0), in the same `code.ips` as the skip and Quick Text.
