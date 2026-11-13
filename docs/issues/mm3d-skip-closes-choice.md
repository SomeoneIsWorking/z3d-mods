# MM3D: the Start skip likely closes a choice unanswered

Expected from the code, not yet seen in a run. OoT3D did exactly this until the skip read the text
type from the script: a choice box reports `TEXT_STATE_CHOICE` only after its text has printed, and
the skip closes a held box on the frame it opens.

Expected: Start runs to the choice and waits for the answer, as on OoT3D
(`docs/plans/cutscene-skip.md`).

Cause: `patch/mm3d/cutscene_skip.cpp` `Game::textIsChoice` returns false, because the layout of
MM3D's `.jdb` scripts (their text entries' type) is not in the mm3d decomp yet.

Fix: find the text entry type in `Cutscene_ProcessCommands` (0x1846B4), name it in the decomp, and
read it as `patch/oot3d/cutscene_script.hpp` does. Prove it on a cutscene that asks a question.
