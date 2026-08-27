# -*- mode: python ; coding: utf-8 -*-
"""Lean PyInstaller spec for 折叠翼遥测查看器 (no collect_all bloat)."""

from pathlib import Path

block_cipher = None
root = Path(SPECPATH)
pkg = root / "telem_viewer"

a = Analysis(
    [str(root / "run_viewer.py")],
    pathex=[str(root)],
    binaries=[],
    datas=[
        (str(pkg / "style.qss"), "telem_viewer"),
    ],
    hiddenimports=[
        "telem_viewer",
        "telem_viewer.__main__",
        "telem_viewer.main_window",
        "telem_viewer.serial_worker",
        "telem_viewer.capture",
        "telem_viewer.cli_replies",
        "telem_viewer.diagnostics",
        "telem_viewer.model",
        "telem_viewer.parser",
        "telem_viewer.paths",
        "serial",
        "serial.tools",
        "serial.tools.list_ports",
        "pyqtgraph",
        "pyqtgraph.graphicsItems",
        "numpy",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "pytest",
        "tkinter",
        "torch",
        "torchvision",
        "tensorflow",
        "pandas",
        "scipy",
        "matplotlib",
        "IPython",
        "jupyter",
        "notebook",
        "PySide6.Qt3DAnimation",
        "PySide6.Qt3DCore",
        "PySide6.Qt3DExtras",
        "PySide6.Qt3DInput",
        "PySide6.Qt3DLogic",
        "PySide6.Qt3DRender",
        "PySide6.QtWebEngineCore",
        "PySide6.QtWebEngineWidgets",
        "PySide6.QtWebEngineQuick",
        "PySide6.QtCharts",
        "PySide6.QtDataVisualization",
        "PySide6.QtMultimedia",
        "PySide6.QtMultimediaWidgets",
        "PySide6.QtBluetooth",
        "PySide6.QtPositioning",
        "PySide6.QtLocation",
        "PySide6.QtSensors",
        "PySide6.QtPdf",
        "PySide6.QtPdfWidgets",
        "pyqtgraph.examples",
        "pyqtgraph.opengl",
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="折叠翼遥测查看器",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="折叠翼遥测查看器",
)
