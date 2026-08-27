from __future__ import annotations

import sys
from pathlib import Path


def app_dir() -> Path:
    """Directory that holds the running app (exe dir when frozen)."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[1]


def resource_dir() -> Path:
    """Bundled read-only resources (PyInstaller _MEIPASS or package dir)."""
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return Path(sys._MEIPASS) / "telem_viewer"
    return Path(__file__).resolve().parent


def style_qss_path() -> Path:
    return resource_dir() / "style.qss"


def default_capture_root() -> Path:
    root = app_dir() / "captures"
    root.mkdir(parents=True, exist_ok=True)
    return root
