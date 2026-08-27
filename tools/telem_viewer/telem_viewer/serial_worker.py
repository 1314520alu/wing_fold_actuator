from __future__ import annotations

import threading

import serial
from PySide6.QtCore import QObject, Signal


class SerialWorker(QObject):
    """Read and write the actuator CLI without blocking the Qt event loop."""

    line_received = Signal(str)
    line_sent = Signal(str)
    error = Signal(str)
    connected = Signal(bool)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._serial: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._write_lock = threading.Lock()

    def open(self, port: str, baud: int = 115200) -> None:
        self.close()
        try:
            self._serial = serial.Serial(port, baudrate=baud, timeout=0.1)
        except (OSError, serial.SerialException) as exc:
            self._serial = None
            self.error.emit(f"无法打开 {port}: {exc}")
            self.connected.emit(False)
            return

        self._stop.clear()
        self._thread = threading.Thread(
            target=self._read_loop,
            name="telem-serial-reader",
            daemon=True,
        )
        self._thread.start()
        self.connected.emit(True)

    def close(self) -> None:
        serial_port = self._serial
        thread = self._thread
        was_open = serial_port is not None
        self._stop.set()
        self._serial = None

        if serial_port is not None:
            try:
                serial_port.close()
            except (OSError, serial.SerialException):
                pass

        if (
            thread is not None
            and thread.is_alive()
            and thread is not threading.current_thread()
        ):
            thread.join(timeout=0.5)
        self._thread = None
        if was_open:
            self.connected.emit(False)

    def send(self, text: str) -> None:
        payload = text if text.endswith("\n") else text + "\n"
        serial_port = self._serial
        if serial_port is None or not serial_port.is_open:
            self.error.emit("串口未连接")
            return
        try:
            with self._write_lock:
                serial_port.write(payload.encode("utf-8"))
            self.line_sent.emit(payload.rstrip("\r\n"))
        except (OSError, serial.SerialException) as exc:
            self.error.emit(f"串口写入失败: {exc}")

    def _read_loop(self) -> None:
        pending = bytearray()
        try:
            while not self._stop.is_set():
                serial_port = self._serial
                if serial_port is None or not serial_port.is_open:
                    break
                chunk = serial_port.read(serial_port.in_waiting or 1)
                if not chunk:
                    continue
                pending.extend(chunk)
                while b"\n" in pending:
                    raw, _, remainder = pending.partition(b"\n")
                    pending = bytearray(remainder)
                    self.line_received.emit(raw.rstrip(b"\r").decode("utf-8", "replace"))
        except (OSError, serial.SerialException) as exc:
            if not self._stop.is_set():
                self.error.emit(f"串口读取失败: {exc}")
        finally:
            if not self._stop.is_set():
                serial_port = self._serial
                self._serial = None
                if serial_port is not None:
                    try:
                        serial_port.close()
                    except (OSError, serial.SerialException):
                        pass
                self.connected.emit(False)
