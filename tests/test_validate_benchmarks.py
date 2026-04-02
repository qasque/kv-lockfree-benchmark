import validate_benchmarks as v


def test_parse_result_line():
    line = "mutex,8,mixed,3,200000,12345,4115000.5,0,0,0,125000,0"
    row = v.parse_result_line(line)
    assert row["impl"] == "mutex"
    assert row["threads"] == "8"
    assert row["profile"] == "mixed"
    assert row["verify_errors"] == "0"
    assert row["ops_per_sec"] == "4115000.5"
