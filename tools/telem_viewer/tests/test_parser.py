from telem_viewer.parser import parse_telem_line


def test_parse_valid_t_line():
    s = parse_telem_line("T,1234,1500,100,200,-100,500,480,0,0,1,0\r\n")
    assert s is not None
    assert s.ms == 1234
    assert s.pwm == 1500
    assert s.count == 100
    assert s.tgt == 200
    assert s.err == -100
    assert s.spd_cmd == 500
    assert s.spd_out == 480
    assert s.hold == 0
    assert s.settled == 0
    assert s.last_dir == 1
    assert s.fault == 0


def test_ignore_status_and_noise():
    assert parse_telem_line("count=1 tgt=2\r\n") is None
    assert parse_telem_line("OK telem on\r\n") is None
    assert parse_telem_line("") is None
