"""Azahar global settings (qt-config.ini), editing only the keys a caller names.

Azahar 2126.2 writes per-game overrides into qt-config.ini at every boot (BootGame's
config->Save() writes each setting's current value), so a per-game value leaks to every game.
"""

from __future__ import annotations

from pathlib import Path


def _format(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def set_global(config_dir: Path, settings: dict[str, dict[str, object]]) -> Path:
    """Set global keys in qt-config.ini; per-game files are not used because of the leak above."""
    path = config_dir / "qt-config.ini"
    lines = path.read_text().splitlines() if path.exists() else []
    for section, values in settings.items():
        header = f"[{section}]"
        if header not in lines:
            lines += ["", header] if lines else [header]
        for key, value in values.items():
            owned = (f"{key}=", f"{key}\\default=")
            lines = [line for line in lines if not line.startswith(owned)]
            start = lines.index(header) + 1
            lines[start:start] = [f"{key}\\default=false", f"{key}={_format(value)}"]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")
    return path
