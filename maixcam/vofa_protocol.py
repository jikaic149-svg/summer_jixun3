"""Compact human-readable VOFA FireWater telemetry emitted by MaixCam."""


VOFA_TCP_PORT = 9001

AXIS_STATE_CODES = {
    "IDLE": 0.0,
    "WAITING": 1.0,
    "CONFIRMED": 2.0,
    "REJECTED": 3.0,
    "TIMEOUT": 4.0,
    "CAN_ERROR": 5.0,
}

MOTOR_MODE_CODES = {
    "IDLE": 0.0,
    "READY": 1.0,
    "RUNNING": 2.0,
    "COMPLETE": 3.0,
    "FAULT": 4.0,
    "UNKNOWN": 5.0,
    "ENABLING": 6.0,
    "DISABLING": 7.0,
}

CHANNEL_NAMES = (
    "time_s",
    "yaw_state",
    "pitch_state",
    "yaw_encoder_raw",
    "pitch_encoder_raw",
    "yaw_actual_deg",
    "pitch_actual_deg",
    "yaw_target_deg",
    "pitch_target_deg",
    "yaw_command_current_a",
    "yaw_feedback_current_a",
    "pitch_command_current_a",
    "pitch_feedback_current_a",
    "motor_mode",
)


def _number(values, key, scale=1.0):
    try:
        return float(values.get(key, 0)) / float(scale)
    except (TypeError, ValueError):
        return 0.0


def make_sample(telemetry, debug):
    """Return the 14 PID-oriented channels from the latest TEL data."""
    del debug
    return (
        _number(telemetry, "ms", 1000.0),
        AXIS_STATE_CODES.get(telemetry.get("yaw_state", ""), -1.0),
        AXIS_STATE_CODES.get(telemetry.get("pitch_state", ""), -1.0),
        _number(telemetry, "yaw_encoder_raw"),
        _number(telemetry, "pitch_encoder_raw"),
        _number(telemetry, "yaw_mdeg", 1000.0),
        _number(telemetry, "pitch_mdeg", 1000.0),
        _number(telemetry, "target_mdeg", 1000.0),
        _number(telemetry, "pitch_target_mdeg", 1000.0),
        _number(telemetry, "cmd_ma", 1000.0),
        _number(telemetry, "fb_ma", 1000.0),
        _number(telemetry, "pitch_cmd_ma", 1000.0),
        _number(telemetry, "pitch_fb_ma", 1000.0),
        MOTOR_MODE_CODES.get(telemetry.get("status", ""), -1.0),
    )


def encode_firewater(sample):
    if len(sample) != len(CHANNEL_NAMES):
        raise ValueError("VOFA channel count mismatch")
    values = ",".join("%.6g" % float(value) for value in sample)
    return ("gimbal: " + values + "\n").encode("ascii")
