from telem_viewer.cli_replies import (
    CalCaptureReply,
    CalShowReply,
    ErrReply,
    MotorFeedbackReply,
    OkReply,
    StatusReply,
    format_motor_feedback,
    parse_cli_reply,
)


def test_parse_status_ok_count():
    line = (
        "count=22278 motor=0 cal=yes enc_fail=0 "
        "pwm=1500 raw=1500 irq=10 age=3ms hold=0 "
        "tgt=12000 spd=500/480 fault=0 backend=htd"
    )
    reply = parse_cli_reply(line)
    assert isinstance(reply, StatusReply)
    assert reply.count == 22278
    assert reply.motor == 0
    assert reply.cal == "yes"
    assert reply.hold == 0
    assert reply.tgt == 12000
    assert reply.spd_cmd == 500
    assert reply.spd_out == 480
    assert reply.fault == 0
    assert reply.pwm == 1500
    assert reply.backend == "htd"


def test_parse_status_without_backend_still_works():
    line = (
        "count=100 motor=0 cal=yes enc_fail=0 "
        "pwm=1500 raw=1500 irq=0 age=0ms hold=0 "
        "tgt=0 spd=0/0 fault=0"
    )
    reply = parse_cli_reply(line)
    assert isinstance(reply, StatusReply)
    assert reply.backend is None


def test_parse_status_encoder_error():
    line = (
        "count=ERR motor=-200 cal=no enc_fail=3 "
        "pwm=0 raw=0 irq=0 age=100ms hold=1 "
        "tgt=0 spd=0/0 fault=1"
    )
    reply = parse_cli_reply(line)
    assert isinstance(reply, StatusReply)
    assert reply.count is None
    assert reply.motor == -200
    assert reply.cal == "no"
    assert reply.enc_fail == 3
    assert reply.fault == 1


def test_parse_cal_show_saved_and_unsaved():
    saved = parse_cli_reply(
        "a=100 b=24000 pwm=1000..2000 dz=50 kp=8 vmax=800 cruise=400 calibrated"
    )
    assert isinstance(saved, CalShowReply)
    assert saved.count_a == 100
    assert saved.count_b == 24000
    assert saved.saved is True
    assert saved.pwm_min == 1000
    assert saved.vmax == 800

    unsaved = parse_cli_reply(
        "a=0 b=0 pwm=1000..2000 dz=50 kp=8 vmax=800 cruise=400 NOT SAVED"
    )
    assert isinstance(unsaved, CalShowReply)
    assert unsaved.saved is False


def test_parse_ok_cal_and_saved():
    capture = parse_cli_reply("OK cal a=12345")
    assert isinstance(capture, CalCaptureReply)
    assert capture.endpoint == "a"
    assert capture.count == 12345

    capture_b = parse_cli_reply("OK cal b=-10")
    assert isinstance(capture_b, CalCaptureReply)
    assert capture_b.endpoint == "b"
    assert capture_b.count == -10

    saved = parse_cli_reply("OK saved")
    assert isinstance(saved, OkReply)
    assert saved.message == "saved"


def test_parse_err_and_ignore_telem():
    err = parse_cli_reply("ERR capture cal a and cal b first")
    assert isinstance(err, ErrReply)
    assert "capture cal a" in err.message

    assert parse_cli_reply("T,0,1500,100,200,100,0,0,0,0,0,0") is None
    assert parse_cli_reply("") is None
    assert parse_cli_reply("hello world") is None


def test_parse_cal_show_with_command_echo_glued():
    """USB-UART echo glues 'cal show' onto the reply without a newline."""
    reply = parse_cli_reply(
        "cal showa=0 b=24000 pwm=1000..2000 dz=150 kp=250 vmax=1000 cruise=1000 calibrated"
    )
    assert isinstance(reply, CalShowReply)
    assert reply.count_a == 0
    assert reply.count_b == 24000
    assert reply.saved is True
    assert reply.kp == 250


def test_parse_ok_with_command_echo_glued():
    reply = parse_cli_reply("telem offOK telem off")
    assert isinstance(reply, OkReply)
    assert reply.message == "telem off"

    reply2 = parse_cli_reply("elem offOK telem off")
    assert isinstance(reply2, OkReply)
    assert reply2.message == "telem off"


def test_parse_ak_link_ok_without_measurements():
    reply = parse_cli_reply("ak link=ok")
    assert isinstance(reply, MotorFeedbackReply)
    assert reply.link_ok is True
    assert reply.voltage is None
    assert format_motor_feedback(reply) == ("电机回传: 已连接", "#9ccc65")

    glued = parse_cli_reply("statusak link=ok")
    assert isinstance(glued, MotorFeedbackReply)
    assert glued.link_ok is True
    assert glued.voltage is None


def test_parse_ak_link_none_and_values():
    none = parse_cli_reply("ak link=none")
    assert isinstance(none, MotorFeedbackReply)
    assert none.link_ok is False
    assert format_motor_feedback(none)[0] == "电机回传: 无应答"

    values = parse_cli_reply("ak link=ok v=24.0 rpm=6300 flt=0 mos=35.2")
    assert isinstance(values, MotorFeedbackReply)
    assert values.voltage == 24.0
    assert values.rpm == 6300
    assert values.fault == 0
    assert values.mos == 35.2
    assert "24.0 V" in format_motor_feedback(values)[0]


def test_parse_status_with_command_echo_glued():
    reply = parse_cli_reply(
        "statuscount=591 motor=0 cal=yes enc_fail=0 "
        "pwm=1006 raw=1006 irq=1 age=2ms hold=0 "
        "tgt=144 spd=-80/-80 fault=0"
    )
    assert isinstance(reply, StatusReply)
    assert reply.count == 591
    assert reply.tgt == 144
