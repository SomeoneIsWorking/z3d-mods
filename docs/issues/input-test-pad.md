# The input tests' gamepad reaches the app but not its presses

Seen 2026-11-17, `z3dmods inputtest picker`. On HEAD's picker (ea286aa) and on the title
stage alike, the uinput pad's left stick and A change nothing: no "picker: on Majora" after the
stick, and the hints stay on the keyboard's caps. The app opens five pads, all named "Xbox 360
Controller" (`[pad] opened`): the test's and four "opensu virtual pad" devices, held by a
separate `opensu` program on this machine that has nothing to do with this project. None of the
four holds a button or an axis, and `opensu` grabs no input device. A log of every pad's left
stick and A in `PadInput::readHost` showed no press from any of them during the test.

The picker test passed with the pad on 2026-11-13 (docs/app.md "Seen"). Not yet known: whether
the test pad's events reach evdev in the Gamescope session (read its device while the test
presses), and whether SDL picks the test pad's evdev node or another driver for the shared
045E:028E IDs.
