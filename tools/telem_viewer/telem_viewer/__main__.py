from __future__ import annotations

import sys

from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication

from telem_viewer.main_window import MainWindow
from telem_viewer.paths import style_qss_path


def _apply_theme(app: QApplication) -> None:
    app.setStyle("Fusion")
    font = QFont("Segoe UI", 10)
    font.setStyleHint(QFont.StyleHint.SansSerif)
    app.setFont(font)
    qss_path = style_qss_path()
    app.setStyleSheet(qss_path.read_text(encoding="utf-8"))


def main() -> None:
    app = QApplication(sys.argv)
    _apply_theme(app)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
