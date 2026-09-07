from __future__ import annotations

from datetime import datetime
from pathlib import Path

import pyqtgraph as pg
from PySide6.QtCore import QPoint, Qt, QTimer, QUrl
from PySide6.QtGui import QCloseEvent, QDesktopServices
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)
from serial.tools import list_ports

from telem_viewer.capture import (
    CaptureSession,
    enrich_rpm,
    load_capture,
)
from telem_viewer.cli_replies import (
    CalCaptureReply,
    CalShowReply,
    ErrReply,
    OkReply,
    StatusReply,
    parse_cli_reply,
)
from telem_viewer.diagnostics import Event, analyze
from telem_viewer.model import SampleBuffer
from telem_viewer.parser import parse_telem_line
from telem_viewer.paths import default_capture_root
from telem_viewer.serial_worker import SerialWorker


class MainWindow(QMainWindow):
    PLOT_REFRESH_MS = 50
    ANALYZE_MS = 1000
    MANUAL_SPEED = 500
    MOTOR_PROFILES: tuple[tuple[str, str], ...] = (
        ("htd", "HTD-85H"),
        ("ak70", "AK70"),
    )
    PROFILE_BUS_HINT: dict[str, str] = {
        "htd": "Lobot 电机模式 · USART1",
        "ak70": "CubeMars 速度环 · USART1",
    }

    def __init__(self):
        super().__init__()
        self.setWindowTitle("折叠翼遥测查看器")
        self.resize(1280, 900)

        self.buffer = SampleBuffer()
        self.events: list[Event] = []
        self._event_keys: set[tuple[int, str]] = set()
        self._capture: CaptureSession | None = None
        self._connected = False
        self._telem_enabled = False
        self._manual_mode = False
        self._jog_speed = 0
        self._plot_dirty = False
        self._board_backend: str | None = None

        self.worker = SerialWorker(self)
        self.worker.line_received.connect(self._on_line)
        self.worker.line_sent.connect(self._on_tx_line)
        self.worker.error.connect(self._on_error)
        self.worker.connected.connect(self._on_connected)

        self._build_ui()
        self._refresh_ports()

        self.plot_timer = QTimer(self)
        self.plot_timer.setInterval(self.PLOT_REFRESH_MS)
        self.plot_timer.timeout.connect(self._refresh_plots_if_needed)
        self.plot_timer.start()

        self.analysis_timer = QTimer(self)
        self.analysis_timer.setInterval(self.ANALYZE_MS)
        self.analysis_timer.timeout.connect(self._run_analysis)
        self.analysis_timer.start()

        self.status_poll_timer = QTimer(self)
        self.status_poll_timer.setInterval(500)
        self.status_poll_timer.timeout.connect(self._poll_status)
        self.status_poll_timer.start()
        self._set_connection_controls(False)

    def _build_ui(self) -> None:
        central = QWidget()
        central.setObjectName("CentralRoot")
        root = QVBoxLayout(central)
        root.setContentsMargins(12, 12, 12, 8)
        root.setSpacing(10)

        root.addWidget(self._build_toolbar())
        root.addWidget(self._build_metric_strip())
        root.addWidget(self._build_cal_panel())

        plot_panel = QWidget()
        plot_grid = QGridLayout(plot_panel)
        plot_grid.setContentsMargins(0, 0, 0, 0)
        plot_grid.setSpacing(8)
        self.plot_widgets: list[pg.PlotWidget] = []
        self.curves: dict[str, pg.PlotDataItem] = {}

        count_plot = self._new_plot("位置", "count")
        self.curves["count"] = count_plot.plot(pen=pg.mkPen("#4FC3F7", width=2), name="count")
        self.curves["tgt"] = count_plot.plot(pen=pg.mkPen("#FFB74D", width=2), name="tgt")

        error_plot = self._new_plot("位置误差", "count")
        self.curves["err"] = error_plot.plot(pen=pg.mkPen("#EF5350", width=2), name="err")

        speed_plot = self._new_plot("速度", "指令 / count/s")
        self.curves["spd_cmd"] = speed_plot.plot(
            pen=pg.mkPen("#AB47BC", width=2), name="spd_cmd"
        )
        self.curves["spd_out"] = speed_plot.plot(
            pen=pg.mkPen("#66BB6A", width=2), name="spd_out"
        )
        self.curves["rpm_est"] = speed_plot.plot(
            pen=pg.mkPen("#EC407A", width=2), name="rpm_est"
        )

        pwm_plot = self._new_plot("PWM 输入", "µs")
        self.curves["pwm"] = pwm_plot.plot(pen=pg.mkPen("#26C6DA", width=2), name="pwm")

        for index, plot in enumerate(self.plot_widgets):
            plot_grid.addWidget(plot, index // 2, index % 2)

        curve_controls = QHBoxLayout()
        curve_label = QLabel("曲线")
        curve_label.setObjectName("SectionHint")
        curve_controls.addWidget(curve_label)
        self.curve_checkboxes: dict[str, QCheckBox] = {}
        for field, curve in self.curves.items():
            checkbox = QCheckBox(field)
            checkbox.setChecked(True)
            checkbox.toggled.connect(curve.setVisible)
            self.curve_checkboxes[field] = checkbox
            curve_controls.addWidget(checkbox)
        curve_controls.addStretch()
        plot_grid.addLayout(curve_controls, 2, 0, 1, 2)

        text_splitter = QSplitter()
        self.event_text = QTextEdit()
        self.event_text.setReadOnly(True)
        self.event_text.setPlaceholderText("诊断事件")
        self.raw_text = QTextEdit()
        self.raw_text.setReadOnly(True)
        self.raw_text.setPlaceholderText("串口原始数据")
        self.raw_text.document().setMaximumBlockCount(5000)
        text_splitter.addWidget(self.event_text)
        text_splitter.addWidget(self.raw_text)

        splitter = QSplitter()
        splitter.setOrientation(Qt.Orientation.Vertical)
        splitter.addWidget(plot_panel)
        splitter.addWidget(text_splitter)
        splitter.setSizes([650, 220])
        root.addWidget(splitter, stretch=1)

        self.setCentralWidget(central)
        self.statusBar().showMessage("未连接")

    def _build_toolbar(self) -> QFrame:
        strip = QFrame()
        strip.setObjectName("ToolbarStrip")
        controls = QHBoxLayout(strip)
        controls.setContentsMargins(12, 10, 12, 10)
        controls.setSpacing(8)

        port_caption = QLabel("串口")
        port_caption.setObjectName("SectionHint")
        controls.addWidget(port_caption)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(140)
        controls.addWidget(self.port_combo)

        motor_caption = QLabel("舵机型号")
        motor_caption.setObjectName("SectionHint")
        controls.addWidget(motor_caption)
        self.motor_profile_combo = QComboBox()
        for _key, label in self.MOTOR_PROFILES:
            self.motor_profile_combo.addItem(label)
        self.motor_profile_combo.setMinimumWidth(110)
        self.motor_profile_combo.currentIndexChanged.connect(
            self._on_profile_changed
        )
        controls.addWidget(self.motor_profile_combo)

        refresh_button = QPushButton("刷新串口")
        refresh_button.clicked.connect(self._refresh_ports)
        controls.addWidget(refresh_button)

        self.connect_button = QPushButton("连接")
        self.connect_button.setObjectName("PrimaryButton")
        self.connect_button.clicked.connect(self._toggle_connection)
        controls.addWidget(self.connect_button)

        self.telem_button = QPushButton("开启遥测")
        self.telem_button.clicked.connect(self._toggle_telem)
        controls.addWidget(self.telem_button)

        self.record_button = QPushButton("录制")
        self.record_button.setObjectName("AccentButton")
        self.record_button.clicked.connect(self._toggle_recording)
        controls.addWidget(self.record_button)

        self.clear_button = QPushButton("清空")
        self.clear_button.clicked.connect(self._clear)
        controls.addWidget(self.clear_button)

        self.open_button = QPushButton("打开采集")
        self.open_button.clicked.connect(self._open_capture)
        controls.addWidget(self.open_button)

        self.export_button = QPushButton("导出")
        self.export_button.clicked.connect(self._export_capture)
        controls.addWidget(self.export_button)

        self.poll_status_checkbox = QCheckBox("轮询 status（500 ms）")
        controls.addWidget(self.poll_status_checkbox)
        controls.addStretch()
        return strip

    def _make_metric_card(self, caption: str) -> tuple[QFrame, QLabel]:
        card = QFrame()
        card.setObjectName("MetricCard")
        card.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(2)
        title = QLabel(caption)
        title.setObjectName("MetricCaption")
        value = QLabel("—")
        value.setObjectName("MetricValue")
        value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(title)
        layout.addWidget(value)
        return card, value

    def _build_metric_strip(self) -> QFrame:
        strip = QFrame()
        strip.setObjectName("MetricStrip")
        row = QHBoxLayout(strip)
        row.setContentsMargins(10, 8, 10, 8)
        row.setSpacing(8)
        self.value_labels: dict[str, QLabel] = {}
        for field, caption in (
            ("count", "编码器 count"),
            ("pwm", "PWM (µs)"),
            ("err", "误差 err"),
            ("spd_cmd", "spd_cmd"),
            ("spd_out", "spd_out"),
        ):
            card, value = self._make_metric_card(caption)
            self.value_labels[field] = value
            row.addWidget(card)
        return strip

    def _build_cal_panel(self) -> QFrame:
        box = QFrame()
        box.setObjectName("CalPanel")
        layout = QVBoxLayout(box)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)

        title = QLabel("标定 / 舵机")
        title.setStyleSheet("font-weight: 600; font-size: 13px; color: #e8edf2;")
        layout.addWidget(title)

        self.bus_hint_label = QLabel(self.PROFILE_BUS_HINT["htd"])
        self.bus_hint_label.setObjectName("SectionHint")
        layout.addWidget(self.bus_hint_label)

        self.backend_mismatch_label = QLabel("")
        self.backend_mismatch_label.setObjectName("SectionHint")
        self.backend_mismatch_label.setStyleSheet("color: #ffb74d;")
        self.backend_mismatch_label.setVisible(False)
        layout.addWidget(self.backend_mismatch_label)

        live_row = QHBoxLayout()
        self.status_labels: dict[str, QLabel] = {}
        for field, caption in (
            ("count", "编码器"),
            ("tgt", "tgt"),
            ("motor", "motor"),
            ("fault", "fault"),
            ("hold", "hold"),
            ("cal", "cal"),
        ):
            card, value = self._make_metric_card(caption)
            value.setStyleSheet("font-size: 14px; font-weight: 600; color: #f2f6fa;")
            self.status_labels[field] = value
            live_row.addWidget(card)
        layout.addLayout(live_row)

        endpoint_row = QHBoxLayout()
        self.cal_labels: dict[str, QLabel] = {}
        for field, caption in (
            ("cal_a", "cal a"),
            ("cal_b", "cal b"),
            ("saved", "保存状态"),
        ):
            card, value = self._make_metric_card(caption)
            value.setStyleSheet("font-size: 14px; font-weight: 600; color: #f2f6fa;")
            self.cal_labels[field] = value
            endpoint_row.addWidget(card)
        endpoint_row.addStretch()
        layout.addLayout(endpoint_row)

        buttons = QHBoxLayout()
        buttons.setSpacing(8)
        self.cal_a_button = QPushButton("采集 A")
        self.cal_a_button.clicked.connect(lambda: self.worker.send("cal a"))
        buttons.addWidget(self.cal_a_button)

        self.cal_b_button = QPushButton("采集 B")
        self.cal_b_button.clicked.connect(lambda: self.worker.send("cal b"))
        buttons.addWidget(self.cal_b_button)

        self.cal_save_button = QPushButton("保存到 Flash")
        self.cal_save_button.setObjectName("DangerButton")
        self.cal_save_button.clicked.connect(self._save_calibration)
        buttons.addWidget(self.cal_save_button)

        self.cal_show_button = QPushButton("读取参数")
        self.cal_show_button.clicked.connect(lambda: self.worker.send("cal show"))
        buttons.addWidget(self.cal_show_button)

        self.refresh_status_button = QPushButton("刷新状态")
        self.refresh_status_button.clicked.connect(lambda: self.worker.send("status"))
        buttons.addWidget(self.refresh_status_button)
        buttons.addStretch()
        layout.addLayout(buttons)

        manual_row = QHBoxLayout()
        manual_row.setSpacing(8)
        self.manual_mode_button = QPushButton("开启手动模式")
        self.manual_mode_button.setObjectName("PrimaryButton")
        self.manual_mode_button.setCheckable(True)
        self.manual_mode_button.toggled.connect(self._on_manual_mode_toggled)
        manual_row.addWidget(self.manual_mode_button)

        self.manual_mode_label = QLabel("控制: PWM 闭环")
        self.manual_mode_label.setObjectName("SectionHint")
        manual_row.addWidget(self.manual_mode_label)

        self.jog_minus_button = QPushButton("−")
        self.jog_minus_button.setObjectName("JogButton")
        self.jog_minus_button.setCheckable(True)
        self.jog_minus_button.setToolTip(
            f"单击开始 motor -{self.MANUAL_SPEED}；再点一次停止"
        )
        self.jog_minus_button.clicked.connect(
            lambda: self._toggle_jog(-self.MANUAL_SPEED)
        )
        manual_row.addWidget(self.jog_minus_button)

        self.jog_plus_button = QPushButton("+")
        self.jog_plus_button.setObjectName("JogButton")
        self.jog_plus_button.setCheckable(True)
        self.jog_plus_button.setToolTip(
            f"单击开始 motor +{self.MANUAL_SPEED}；再点一次停止"
        )
        self.jog_plus_button.clicked.connect(
            lambda: self._toggle_jog(self.MANUAL_SPEED)
        )
        manual_row.addWidget(self.jog_plus_button)

        jog_hint = QLabel(f"点动 ±{self.MANUAL_SPEED}（再点停止）")
        jog_hint.setObjectName("SectionHint")
        manual_row.addWidget(jog_hint)
        manual_row.addStretch()
        layout.addLayout(manual_row)

        self._cal_buttons = [
            self.cal_a_button,
            self.cal_b_button,
            self.cal_save_button,
            self.cal_show_button,
            self.refresh_status_button,
            self.manual_mode_button,
        ]
        self._jog_buttons = [self.jog_minus_button, self.jog_plus_button]
        return box

    def _new_plot(self, title: str, y_label: str) -> pg.PlotWidget:
        plot = pg.PlotWidget(title=title)
        plot.setBackground("#0f1419")
        plot.setLabel("bottom", "时间", units="ms")
        plot.setLabel("left", y_label)
        plot.showGrid(x=True, y=True, alpha=0.2)
        plot.addLegend()
        axis_pen = pg.mkPen("#5a6a7a")
        for axis_name in ("left", "bottom"):
            axis = plot.getAxis(axis_name)
            axis.setPen(axis_pen)
            axis.setTextPen("#9aa7b5")
        plot.setTitle(title, color="#c5d0db", size="12pt")
        self.plot_widgets.append(plot)
        return plot

    def _refresh_ports(self) -> None:
        selected = self.port_combo.currentText()
        ports = [port.device for port in list_ports.comports()]
        self.port_combo.clear()
        self.port_combo.addItems(ports)
        if selected in ports:
            self.port_combo.setCurrentText(selected)

    def _toggle_connection(self) -> None:
        if self._connected:
            if self._manual_mode:
                self.worker.send("hold")
                self._set_manual_mode_ui(False)
            if self._telem_enabled:
                self.worker.send("telem off")
            self.worker.close()
            return
        port = self.port_combo.currentText().strip()
        if not port:
            self._on_error("请先选择串口")
            return
        self.statusBar().showMessage(f"正在连接 {port}…")
        self.worker.open(port)

    def _on_connected(self, connected: bool) -> None:
        self._connected = connected
        self.connect_button.setText("断开" if connected else "连接")
        self.connect_button.setObjectName("DangerButton" if connected else "PrimaryButton")
        self.connect_button.style().unpolish(self.connect_button)
        self.connect_button.style().polish(self.connect_button)
        if not connected:
            self._telem_enabled = False
            self.telem_button.setText("开启遥测")
            self._set_manual_mode_ui(False)
            self._set_connection_controls(False)
            self._board_backend = None
            self.backend_mismatch_label.setVisible(False)
            self.statusBar().showMessage("未连接")
            return
        self._set_connection_controls(True)
        self.statusBar().showMessage("已连接")
        self.worker.send("cal show")
        self.worker.send("status")

    def _profile_key(self, index: int | None = None) -> str:
        idx = self.motor_profile_combo.currentIndex() if index is None else index
        if idx < 0 or idx >= len(self.MOTOR_PROFILES):
            return "htd"
        return self.MOTOR_PROFILES[idx][0]

    def _profile_index(self, key: str) -> int:
        for index, (profile_key, _label) in enumerate(self.MOTOR_PROFILES):
            if profile_key == key:
                return index
        return -1

    def _select_profile(self, key: str, *, from_board: bool = False) -> None:
        index = self._profile_index(key)
        if index < 0:
            if from_board:
                self.backend_mismatch_label.setText(
                    f"⚠ 板子固件={key}（未知型号）"
                )
                self.backend_mismatch_label.setVisible(True)
            return
        self.motor_profile_combo.blockSignals(True)
        self.motor_profile_combo.setCurrentIndex(index)
        self.motor_profile_combo.blockSignals(False)
        self._update_bus_hint()
        if from_board:
            self._board_backend = key
        self._update_backend_mismatch()

    def _on_profile_changed(self, _index: int) -> None:
        self._update_bus_hint()
        self._update_backend_mismatch()

    def _update_bus_hint(self) -> None:
        key = self._profile_key()
        self.bus_hint_label.setText(
            self.PROFILE_BUS_HINT.get(key, "未知舵机总线")
        )

    def _update_backend_mismatch(self) -> None:
        selected = self._profile_key()
        if self._board_backend and self._board_backend != selected:
            self.backend_mismatch_label.setText(
                f"⚠ 板子固件={self._board_backend}，界面={selected}"
            )
            self.backend_mismatch_label.setVisible(True)
        else:
            self.backend_mismatch_label.setVisible(False)

    def _apply_board_backend(self, backend: str | None) -> None:
        if not backend:
            return
        self._select_profile(backend, from_board=True)

    def _set_connection_controls(self, connected: bool) -> None:
        self.telem_button.setEnabled(connected)
        self.record_button.setEnabled(connected or self._capture is not None)
        self.poll_status_checkbox.setEnabled(connected)
        for button in self._cal_buttons:
            button.setEnabled(connected)
        for button in self._jog_buttons:
            button.setEnabled(connected and self._manual_mode)

    def _set_manual_mode_ui(self, enabled: bool) -> None:
        self._manual_mode = enabled
        if not enabled:
            self._jog_speed = 0
        self.manual_mode_button.blockSignals(True)
        self.manual_mode_button.setChecked(enabled)
        self.manual_mode_button.blockSignals(False)
        self.manual_mode_button.setText("退出手动模式" if enabled else "开启手动模式")
        self.manual_mode_label.setText(
            "控制: 手动（PWM 闭环暂停）" if enabled else "控制: PWM 闭环"
        )
        for button in self._jog_buttons:
            button.setEnabled(self._connected and enabled)
        self._sync_jog_button_state()

    def _on_manual_mode_toggled(self, enabled: bool) -> None:
        if not self._connected:
            self._set_manual_mode_ui(False)
            return
        if enabled:
            # motor 0 keeps firmware manual override so PWM closed-loop is bypassed.
            self._jog_speed = 0
            self.worker.send("motor 0")
            self._set_manual_mode_ui(True)
            self.statusBar().showMessage(
                f"已进入手动模式：PWM 暂停，± 单击切换，速度 {self.MANUAL_SPEED}",
                5000,
            )
            return
        self._jog_speed = 0
        self.worker.send("hold")
        self._set_manual_mode_ui(False)
        self.statusBar().showMessage("已退出手动模式：恢复 PWM 闭环", 5000)

    def _sync_jog_button_state(self) -> None:
        self.jog_plus_button.blockSignals(True)
        self.jog_minus_button.blockSignals(True)
        self.jog_plus_button.setChecked(self._jog_speed > 0)
        self.jog_minus_button.setChecked(self._jog_speed < 0)
        self.jog_plus_button.blockSignals(False)
        self.jog_minus_button.blockSignals(False)

    def _toggle_jog(self, speed: int) -> None:
        """Click once to run at ±speed; click same direction again to stop."""
        if not self._connected or not self._manual_mode:
            self._sync_jog_button_state()
            return
        if self._jog_speed == speed:
            self._jog_speed = 0
            self.worker.send("motor 0")
            self.statusBar().showMessage("点动已停止", 2000)
        else:
            self._jog_speed = speed
            self.worker.send(f"motor {speed}")
            self.statusBar().showMessage(f"点动中 motor {speed}（再点同向停止）", 3000)
        self._sync_jog_button_state()

    def _toggle_telem(self) -> None:
        self._telem_enabled = not self._telem_enabled
        self.worker.send("telem on" if self._telem_enabled else "telem off")
        self.telem_button.setText("关闭遥测" if self._telem_enabled else "开启遥测")

    def _toggle_recording(self) -> None:
        if self._capture is not None:
            self._stop_recording()
            return
        capture_root = default_capture_root()
        capture_path = CaptureSession.start(capture_root)
        self._capture = CaptureSession.active()
        self.record_button.setText("停止录制")
        self.statusBar().showMessage(f"正在录制到 {capture_path}")

    def _stop_recording(self) -> None:
        if self._capture is None:
            return
        capture_path = self._capture.path
        self._capture.write_events(self.events)
        self._capture.close()
        self._capture = None
        self.record_button.setText("录制")
        self.statusBar().showMessage(f"采集已保存: {capture_path}")

    def _save_calibration(self) -> None:
        answer = QMessageBox.question(
            self,
            "保存到 Flash",
            "将把当前 cal a / cal b 写入 MCU Flash，掉电后仍保留。\n确认保存？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self.worker.send("cal save")

    def _on_line(self, line: str) -> None:
        self._log_serial("<<", line)

        stripped = line.strip()
        if stripped.startswith("boot backend="):
            self._apply_board_backend(stripped.split("=", 1)[1].strip())

        sample = parse_telem_line(line)
        if sample is not None:
            if self.buffer.samples:
                previous = self.buffer.samples[-1]
                previous_rpm = previous.rpm_est
                enrich_rpm([previous, sample])
                previous.rpm_est = previous_rpm
            else:
                enrich_rpm([sample])

            self.buffer.append(sample)
            if self._capture is not None:
                self._capture.write_sample(sample)
            for field, label in self.value_labels.items():
                label.setText(str(getattr(sample, field)))
            self.status_labels["count"].setText(str(sample.count))
            self.status_labels["tgt"].setText(str(sample.tgt))
            self.status_labels["fault"].setText(str(sample.fault))
            self.status_labels["hold"].setText(str(sample.hold))
            self._plot_dirty = True
            return

        reply = parse_cli_reply(line)
        if reply is not None:
            self._apply_cli_reply(reply)

    def _apply_cli_reply(
        self,
        reply: StatusReply | CalShowReply | CalCaptureReply | ErrReply | OkReply,
    ) -> None:
        if isinstance(reply, StatusReply):
            count_text = "ERR" if reply.count is None else str(reply.count)
            self.status_labels["count"].setText(count_text)
            self.status_labels["tgt"].setText(str(reply.tgt))
            self.status_labels["motor"].setText(str(reply.motor))
            self.status_labels["fault"].setText(str(reply.fault))
            self.status_labels["hold"].setText(str(reply.hold))
            self.status_labels["cal"].setText(reply.cal)
            if reply.backend is not None:
                self._apply_board_backend(reply.backend)
            if reply.count is not None:
                self.value_labels["count"].setText(str(reply.count))
            if reply.pwm is not None:
                self.value_labels["pwm"].setText(str(reply.pwm))
            self.value_labels["spd_cmd"].setText(str(reply.spd_cmd))
            self.value_labels["spd_out"].setText(str(reply.spd_out))
            return

        if isinstance(reply, CalShowReply):
            self.cal_labels["cal_a"].setText(str(reply.count_a))
            self.cal_labels["cal_b"].setText(str(reply.count_b))
            self.cal_labels["saved"].setText("已保存" if reply.saved else "未保存")
            self.status_labels["cal"].setText("yes" if reply.saved else "no")
            return

        if isinstance(reply, CalCaptureReply):
            key = "cal_a" if reply.endpoint == "a" else "cal_b"
            self.cal_labels[key].setText(str(reply.count))
            self.cal_labels["saved"].setText("未保存")
            self.status_labels["cal"].setText("no")
            self.statusBar().showMessage(
                f"已采集 cal {reply.endpoint}={reply.count}（尚未写入 Flash）", 5000
            )
            return

        if isinstance(reply, ErrReply):
            self._on_error(reply.message)
            return

        if isinstance(reply, OkReply):
            if reply.message == "saved":
                self.cal_labels["saved"].setText("已保存")
                self.status_labels["cal"].setText("yes")
                self.statusBar().showMessage("标定已写入 Flash", 5000)
                self.worker.send("cal show")
            else:
                self.statusBar().showMessage(f"OK {reply.message}", 3000)

    def _on_tx_line(self, line: str) -> None:
        self._log_serial(">>", line)

    def _log_serial(self, direction: str, line: str) -> None:
        timestamp = datetime.now().isoformat(timespec="milliseconds")
        rendered = f"{timestamp} {direction} {line}"
        self.raw_text.append(rendered)
        if self._capture is not None:
            self._capture.write_raw(f"{direction} {line}")

    def _refresh_plots_if_needed(self) -> None:
        if not self._plot_dirty:
            return
        data = self.buffer.as_lists()
        x = data["ms"]
        for field, curve in self.curves.items():
            values = data[field]
            if field == "rpm_est":
                values = [float("nan") if value is None else value for value in values]
            curve.setData(x, values)
        self._plot_dirty = False

    def _run_analysis(self) -> None:
        found = analyze(self.buffer.samples[-500:])
        changed = False
        for event in found:
            key = (event.t_ms, event.type)
            if key in self._event_keys:
                continue
            self._event_keys.add(key)
            self.events.append(event)
            self.event_text.append(
                f"[{event.t_ms} ms] {event.type}: {event.message}\n"
                f"  建议: {event.hint}"
            )
            changed = True
        if changed and self._capture is not None:
            self._capture.write_events(self.events)

    def _poll_status(self) -> None:
        if self._connected and self.poll_status_checkbox.isChecked():
            self.worker.send("status")

    def _clear(self) -> None:
        self._stop_recording()
        self.buffer.clear()
        self.events.clear()
        self._event_keys.clear()
        self.event_text.clear()
        self.raw_text.clear()
        for curve in self.curves.values():
            curve.setData([], [])
        for label in self.value_labels.values():
            label.setText("—")
        for field in ("count", "tgt", "motor", "fault", "hold"):
            self.status_labels[field].setText("—")
        self._plot_dirty = False

    def _open_capture(self) -> None:
        menu = QMenu(self)
        pick_csv = menu.addAction("选择 telem.csv…")
        pick_dir = menu.addAction("选择采集文件夹…")
        chosen = menu.exec(
            self.open_button.mapToGlobal(QPoint(0, self.open_button.height()))
        )
        if chosen is pick_csv:
            path, _ = QFileDialog.getOpenFileName(
                self,
                "打开遥测采集",
                "",
                "采集文件 (telem.csv);;所有文件 (*)",
            )
        elif chosen is pick_dir:
            path = QFileDialog.getExistingDirectory(
                self, "打开遥测采集文件夹"
            )
        else:
            return
        if not path:
            return
        try:
            self._load_capture_path(path)
        except (OSError, ValueError, KeyError) as exc:
            self._on_error(f"无法打开采集: {exc}")

    def _load_capture_path(self, path: str | Path) -> None:
        samples, stored_events = load_capture(path)
        analyzed_events = analyze(samples)
        merged: list[Event] = []
        keys: set[tuple[int, str]] = set()
        for event in [*stored_events, *analyzed_events]:
            key = (event.t_ms, event.type)
            if key not in keys:
                keys.add(key)
                merged.append(event)
        merged.sort(key=lambda event: event.t_ms)

        self._clear()
        self.buffer.samples = samples[-self.buffer.maxlen :]
        self.events = merged
        self._event_keys = keys
        for event in merged:
            self.event_text.append(
                f"[{event.t_ms} ms] {event.type}: {event.message}\n"
                f"  建议: {event.hint}"
            )
        if self.buffer.samples:
            latest = self.buffer.samples[-1]
            for field, label in self.value_labels.items():
                label.setText(str(getattr(latest, field)))
            self.status_labels["count"].setText(str(latest.count))
            self.status_labels["tgt"].setText(str(latest.tgt))
            self.status_labels["fault"].setText(str(latest.fault))
            self.status_labels["hold"].setText(str(latest.hold))
        self._plot_dirty = True
        root = Path(path).parent if Path(path).is_file() else Path(path)
        self.statusBar().showMessage(f"已从 {root} 加载 {len(samples)} 个样本")

    def _export_capture(self) -> None:
        if self._capture is not None:
            capture_path = self._capture.path
            self._stop_recording()
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(capture_path)))
            return
        if not self.buffer.samples:
            self._on_error("没有可导出的遥测样本")
            return
        directory = QFileDialog.getExistingDirectory(self, "导出遥测采集")
        if not directory:
            return
        try:
            capture_path = CaptureSession.start(directory)
            session = CaptureSession.active()
            for sample in self.buffer.samples:
                session.write_sample(sample)
            session.write_events(self.events)
            session.close()
        except (OSError, RuntimeError) as exc:
            self._on_error(f"无法导出采集: {exc}")
            return
        self.statusBar().showMessage(f"已导出采集: {capture_path}")
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(capture_path)))

    def _on_error(self, message: str) -> None:
        self.raw_text.append(f"错误: {message}")
        self.statusBar().showMessage(message, 8000)

    def closeEvent(self, event: QCloseEvent) -> None:
        self._stop_recording()
        if self._connected and self._manual_mode:
            self.worker.send("hold")
            self._set_manual_mode_ui(False)
        if self._connected and self._telem_enabled:
            self.worker.send("telem off")
        self.worker.close()
        super().closeEvent(event)
