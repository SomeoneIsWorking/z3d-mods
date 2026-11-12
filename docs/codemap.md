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
- `sets/`: per-game mod set manifests.
