import os
from pathlib import Path

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


@pytest.fixture(scope="module")
def qapp():
    QApplication = pytest.importorskip("PySide6.QtWidgets").QApplication
    app = QApplication.instance() or QApplication([])
    yield app


def test_task_5_modules_exist():
    package = Path(__file__).parents[1] / "telem_viewer"
    assert (package / "serial_worker.py").is_file()
    assert (package / "main_window.py").is_file()
    assert (package / "__main__.py").is_file()


def test_serial_worker_appends_newline(qapp):
    from telem_viewer.serial_worker import SerialWorker

    class FakeSerial:
        is_open = True

        def __init__(self):
            self.writes = []

        def write(self, payload):
            self.writes.append(payload)

    worker = SerialWorker()
    fake = FakeSerial()
    worker._serial = fake
    sent = []
    worker.line_sent.connect(sent.append)

    worker.send("telem on")
    worker.send("status\n")

    assert fake.writes == [b"telem on\n", b"status\n"]
    assert sent == ["telem on", "status"]


def test_main_window_has_required_controls_and_plots(qapp):
    from telem_viewer.main_window import MainWindow

    window = MainWindow()

    assert window.port_combo is not None
    assert window.connect_button.text() == "连接"
    assert window.telem_button.text() == "开启遥测"
    assert window.record_button.text() == "录制"
    assert window.clear_button.text() == "清空"
    assert window.open_button.text() == "打开采集"
    assert window.export_button.text() == "导出"
    assert window.poll_status_checkbox.isChecked() is False
    assert len(window.plot_widgets) == 4
    assert set(window.value_labels) == {"count", "pwm", "err", "spd_cmd", "spd_out"}
    assert set(window.curve_checkboxes) == set(window.curves)
    assert window.cal_a_button.text() == "采集 A"
    assert window.cal_b_button.text() == "采集 B"
    assert window.cal_save_button.text() == "保存到 Flash"
    assert window.cal_show_button.text() == "读取参数"
    assert window.refresh_status_button.text() == "刷新状态"
    assert window.cal_a_button.isEnabled() is False
    assert window.cal_save_button.isEnabled() is False
    assert window.manual_mode_button.text() == "开启手动模式"
    assert window.jog_plus_button.isEnabled() is False
    assert window.jog_minus_button.isEnabled() is False
    window.close()


def test_manual_mode_sends_motor_override_and_hold(qapp):
    from telem_viewer.main_window import MainWindow

    window = MainWindow()
    sent: list[str] = []
    window.worker.send = sent.append  # type: ignore[method-assign]
    window._connected = True
    window._set_connection_controls(True)

    window.manual_mode_button.setChecked(True)
    assert window._manual_mode is True
    assert sent == ["motor 0"]
    assert window.jog_plus_button.isEnabled() is True
    assert window.manual_mode_button.text() == "退出手动模式"

    window._toggle_jog(window.MANUAL_SPEED)
    assert window._jog_speed == window.MANUAL_SPEED
    assert sent[-1] == "motor 500"
    assert window.jog_plus_button.isChecked() is True

    window._toggle_jog(window.MANUAL_SPEED)
    assert window._jog_speed == 0
    assert sent[-1] == "motor 0"
    assert window.jog_plus_button.isChecked() is False

    window._toggle_jog(-window.MANUAL_SPEED)
    assert window._jog_speed == -window.MANUAL_SPEED
    assert sent[-1] == "motor -500"
    window._toggle_jog(window.MANUAL_SPEED)
    assert window._jog_speed == window.MANUAL_SPEED
    assert sent[-1] == "motor 500"

    window.manual_mode_button.setChecked(False)
    assert window._manual_mode is False
    assert window._jog_speed == 0
    assert sent[-1] == "hold"
    assert window.jog_plus_button.isEnabled() is False
    window.close()


def test_main_window_applies_status_and_cal_replies(qapp):
    from telem_viewer.main_window import MainWindow

    window = MainWindow()
    window._on_line(
        "count=22278 motor=12 cal=yes enc_fail=0 "
        "pwm=1500 raw=1500 irq=1 age=2ms hold=0 "
        "tgt=12000 spd=500/480 fault=0"
    )
    assert window.status_labels["count"].text() == "22278"
    assert window.status_labels["motor"].text() == "12"
    assert window.status_labels["cal"].text() == "yes"
    assert window.value_labels["count"].text() == "22278"

    window._on_line(
        "a=100 b=24000 pwm=1000..2000 dz=50 kp=8 vmax=800 cruise=400 calibrated"
    )
    assert window.cal_labels["cal_a"].text() == "100"
    assert window.cal_labels["cal_b"].text() == "24000"
    assert window.cal_labels["saved"].text() == "已保存"

    window._on_line("OK cal a=111")
    assert window.cal_labels["cal_a"].text() == "111"
    assert window.cal_labels["saved"].text() == "未保存"
    window.close()


def test_main_window_enriches_consecutive_samples(qapp):
    from telem_viewer.main_window import MainWindow

    window = MainWindow()

    window._on_line("T,0,1500,100,200,100,500,480,0,0,1,0")
    window._on_line("T,10,1500,110,200,90,500,480,0,0,1,0")

    assert len(window.buffer.samples) == 2
    assert window.buffer.samples[0].rpm_est is None
    assert window.buffer.samples[1].rpm_est == pytest.approx(1000.0)
    assert window.value_labels["pwm"].text() == "1500"
    assert window.value_labels["err"].text() == "90"
    assert window.value_labels["count"].text() == "110"
    assert window.status_labels["count"].text() == "110"
    assert "<< T,10,1500" in window.raw_text.toPlainText()
    window.close()


def test_main_window_logs_tx_with_direction_prefix(qapp):
    from telem_viewer.main_window import MainWindow

    window = MainWindow()
    window._on_tx_line("status")

    assert ">> status" in window.raw_text.toPlainText()
    window.close()


def test_replay_reanalyzes_full_capture_and_merges_stored_events(qapp, tmp_path):
    from telem_viewer.capture import CaptureSession
    from telem_viewer.diagnostics import Event
    from telem_viewer.main_window import MainWindow
    from telem_viewer.model import Sample

    capture_dir = CaptureSession.start(tmp_path)
    session = CaptureSession.active()
    samples = [
        Sample(0, 1000, 0, 100, 100, 0, 0, 0, 0, 1, 0),
        Sample(10, 1200, 1, 100, 99, 0, 0, 0, 0, 1, 0),
    ]
    samples.extend(
        Sample(ms, 1200, ms, 100 + ms, 0, 0, 0, 0, 1, 1, 0)
        for ms in range(20, 6020, 10)
    )
    for sample in samples:
        session.write_sample(sample)
    session.write_events([Event(5, "STALE", "stored", "stored hint")])
    session.close()

    window = MainWindow()
    window._load_capture_path(capture_dir / "telem.csv")

    event_types = {event.type for event in window.events}
    assert "PWM_GLITCH" in event_types
    assert "STALE" in event_types
    window.close()
