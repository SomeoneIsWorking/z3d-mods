# Codemap

- `tools/z3dmods/cli.py`: the `z3dmods` commands; composes the modules below.
- `tools/z3dmods/config.py`: the one reader of the environment and `env-assets/local.json`.
- `tools/z3dmods/ctr.py`: decrypted CCI/NCCH/exheader/ExeFS reading and `.code` LZ decompression.
- `tools/z3dmods/ips.py`: IPS parse, apply and build.
- `tools/z3dmods/bps.py`: BPS info and apply, with source and target CRC checks.
- `tools/z3dmods/install.py`: installs a `sets/<game>.json` set into Azahar's `load/`, with an install record.
- `tools/z3dmods/azahar_config.py`: global `qt-config.ini` edits.
- `tools/z3dmods/azahar.py`: the headless Azahar session (Gamescope, XTest keys, RPC RAM and frames, config restore).
- `tools/z3dmods/steps.py`: the scripted step language for runs.
- `tools/z3dmods/games.py`: per-game guest RAM layout the run steps read (play state, save context, cutscene).
- `tools/z3dmods/patch_build.py`: builds our MIT patch (`patch/<game>/`) into IPS records with clang and lld.
- `patch/common/`: guest memory and call helpers for the patches.
- `patch/oot3d/`: our OoT3D patch: guest layout (`game.hpp`), the Start cutscene skip, hook sites and link script.
- `sets/`: per-game mod set manifests.
