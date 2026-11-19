# Every platform, every milestone

User: "a GitHub release on every milestone, all possible platforms, macOS (.app), Linux
(.AppImage), Windows (.exe), Android (.apk)", "touch controls for Android when not using a
controller (hotswap)". The rules are the `release` and `android` skills.

## Where each platform stands
- Linux: released (`z3d-x86_64.AppImage`, `z3dmods package`, `.github/workflows/release.yml`).
- macOS: builds and plays from a checkout on Apple silicon (AGENTS.md); `z3dmods shortcut` makes a
  local `~/Applications/z3d.app` that runs the checkout. No self-contained `.app` yet.
- Windows: never built. The app is SDL3 and Vulkan, which Windows has; the core builds on Windows
  upstream.
- Android: never built. Azahar's core runs on Android upstream; our frontend is SDL3, which has an
  Android backend. The setup runs as Python beside the app (plans/packed-app.md), which Android
  cannot carry, so the install needs a native path there.

## Slices
1. macOS `.app`: the package's folder layout for macOS (`z3dmods package` on a Mac): the app, the
   core and MoltenVK inside `Contents/`, the tools and their Python runtime in `Contents/Resources`,
   ad-hoc signed, zipped. Built on the Mac by the heavy queue's worker, which runs a checked-in
   `tools/` script (psxport `tools/heavy.py`, the Mac cannot be reached from here).
2. Windows `.exe`: the app and the tools building on Windows (POSIX calls behind their owners:
   `tool_process`, paths, the user folder), then the package as a folder with `z3d.exe` in a zip,
   built by the release workflow on a hosted Windows runner (started by hand or by the tag).
3. Android `.apk`: an SDL3 Android activity around the same app, the core's Android build, Vulkan;
   the setup run natively (the install's rules in C++ only where Python cannot go, one owner each),
   and the touch layer: the 3DS buttons and sticks drawn over the game, multi-touch, hidden while a
   controller is in use and shown again on the next touch.
4. The release: one tag builds all four and attaches them; the release notes name each file.
