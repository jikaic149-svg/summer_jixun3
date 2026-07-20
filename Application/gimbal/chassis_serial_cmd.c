#include "chassis_serial_cmd.h"

#include "gimbal_task.h"

static uint8_t cmd_to_lower(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') {
        return (uint8_t)(c + ('a' - 'A'));
    }
    return c;
}

static uint8_t cmd_is_delimiter(uint8_t c)
{
    return (c == ' ') || (c == '\t') || (c == ':') ||
           (c == '=') || (c == ',');
}

static uint8_t cmd_only_delimiters(const uint8_t *line,
                                   uint8_t len,
                                   uint8_t start)
{
    while (start < len) {
        if (cmd_is_delimiter(line[start]) == 0U) {
            return 0U;
        }
        start++;
    }
    return 1U;
}

static uint8_t cmd_match_word(const uint8_t *line,
                              uint8_t len,
                              uint8_t start,
                              const char *word)
{
    uint8_t i = 0U;
    uint8_t end;

    if ((line == 0) || (word == 0)) {
        return 0U;
    }

    while (word[i] != '\0') {
        if ((uint8_t)(start + i) >= len) {
            return 0U;
        }
        if (cmd_to_lower(line[start + i]) != (uint8_t)word[i]) {
            return 0U;
        }
        i++;
    }

    end = (uint8_t)(start + i);
    if (cmd_only_delimiters(line, len, end) == 0U) {
        return 0U;
    }

    return 1U;
}

static uint8_t cmd_parse_float(const uint8_t *line, uint8_t len, float *value_out)
{
    uint8_t i = 0U;
    uint8_t has_digit = 0U;
    uint8_t has_frac = 0U;
    float sign = 1.0f;
    float value = 0.0f;
    float frac_scale = 0.1f;

    if (line == 0 || value_out == 0 || len == 0U) {
        return 0U;
    }

    while (i < len) {
        uint8_t c = line[i];
        if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            break;
        }
        i++;
    }

    if (i >= len) {
        return 0U;
    }

    if (line[i] == '-') {
        sign = -1.0f;
        i++;
    } else if (line[i] == '+') {
        i++;
    }

    while (i < len) {
        uint8_t c = line[i];
        if (c >= '0' && c <= '9') {
            has_digit = 1U;
            if (has_frac == 0U) {
                value = value * 10.0f + (float)(c - '0');
            } else {
                value += (float)(c - '0') * frac_scale;
                frac_scale *= 0.1f;
            }
        } else if (c == '.' && has_frac == 0U) {
            has_frac = 1U;
        } else {
            break;
        }
        i++;
    }

    if ((has_digit == 0U) ||
        (cmd_only_delimiters(line, len, i) == 0U)) {
        return 0U;
    }

    *value_out = sign * value;
    return 1U;
}

static uint8_t cmd_parse_bias(const uint8_t *line, uint8_t len, float *bias_deg)
{
    uint8_t i = 0U;

    if (line == 0 || bias_deg == 0 || len == 0U) {
        return 0U;
    }

    while ((i < len) && line[i] == ' ') {
        i++;
    }

    if (i >= len) {
        return 0U;
    }

    if (line[i] == 'B' || line[i] == 'b') {
        i++;
        return cmd_parse_float(line + i, (uint8_t)(len - i), bias_deg);
    }

    if ((i + 4U <= len) &&
        (line[i] == 'b' || line[i] == 'B') &&
        (line[i + 1U] == 'i' || line[i + 1U] == 'I') &&
        (line[i + 2U] == 'a' || line[i + 2U] == 'A') &&
        (line[i + 3U] == 's' || line[i + 3U] == 'S')) {
        i += 4U;
        return cmd_parse_float(line + i, (uint8_t)(len - i), bias_deg);
    }

    return 0U;
}

static uint8_t cmd_mode_from_digit(uint8_t c, ChassisMode_e *mode)
{
    if ((mode == 0) || (c < '0') || (c > '7')) {
        return 0U;
    }

    *mode = (ChassisMode_e)(c - '0');
    return 1U;
}

static uint8_t cmd_mode_from_word(const uint8_t *line,
                                  uint8_t len,
                                  uint8_t start,
                                  ChassisMode_e *mode)
{
    if (mode == 0) {
        return 0U;
    }

    if ((cmd_match_word(line, len, start, "stand") != 0U) ||
        (cmd_match_word(line, len, start, "balance") != 0U) ||
        (cmd_match_word(line, len, start, "vision") != 0U)) {
        *mode = CHASSIS_MODE_STAND;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "forward") != 0U) ||
        (cmd_match_word(line, len, start, "go") != 0U)) {
        *mode = CHASSIS_MODE_FORWARD;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "backward") != 0U) ||
        (cmd_match_word(line, len, start, "back") != 0U) ||
        (cmd_match_word(line, len, start, "reverse") != 0U)) {
        *mode = CHASSIS_MODE_BACKWARD;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "stop") != 0U) ||
        (cmd_match_word(line, len, start, "park") != 0U)) {
        *mode = CHASSIS_MODE_STAND;
        return 1U;
    }
    if (cmd_match_word(line, len, start, "left") != 0U) {
        *mode = CHASSIS_MODE_TURN_LEFT;
        return 1U;
    }
    if (cmd_match_word(line, len, start, "right") != 0U) {
        *mode = CHASSIS_MODE_TURN_RIGHT;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "track") != 0U) ||
        (cmd_match_word(line, len, start, "tracking") != 0U) ||
        (cmd_match_word(line, len, start, "line") != 0U)) {
        *mode = CHASSIS_MODE_TRACKING;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "square") != 0U) ||
        (cmd_match_word(line, len, start, "box") != 0U)) {
        *mode = CHASSIS_MODE_SQUARE_TRACKING;
        return 1U;
    }
    if ((cmd_match_word(line, len, start, "shoot") != 0U) ||
        (cmd_match_word(line, len, start, "aim") != 0U) ||
        (cmd_match_word(line, len, start, "target") != 0U)) {
        *mode = CHASSIS_MODE_TARGET_AIM;
        return 1U;
    }

    return 0U;
}

static uint8_t cmd_parse_mode(const uint8_t *line,
                              uint8_t len,
                              ChassisMode_e *mode)
{
    uint8_t i = 0U;

    if ((line == 0) || (mode == 0) || (len == 0U)) {
        return 0U;
    }

    while ((i < len) && (cmd_is_delimiter(line[i]) != 0U)) {
        i++;
    }
    if (i >= len) {
        return 0U;
    }

    if (cmd_mode_from_word(line, len, i, mode) != 0U) {
        return 1U;
    }

    /* Bare 6/7 remain wheel-speed commands. New task modes require a word,
     * mode N, or mN so numeric telemetry cannot switch modes accidentally. */
    if ((line[i] <= '5') &&
        (cmd_mode_from_digit(line[i], mode) != 0U) &&
        (cmd_only_delimiters(line, len, (uint8_t)(i + 1U)) != 0U)) {
        return 1U;
    }

    if (cmd_to_lower(line[i]) != 'm') {
        return 0U;
    }

    i++;
    if ((i + 3U <= len) &&
        (cmd_to_lower(line[i]) == 'o') &&
        (cmd_to_lower(line[i + 1U]) == 'd') &&
        (cmd_to_lower(line[i + 2U]) == 'e')) {
        i += 3U;
    }

    while ((i < len) && (cmd_is_delimiter(line[i]) != 0U)) {
        i++;
    }
    if (i >= len) {
        return 0U;
    }

    if ((cmd_mode_from_digit(line[i], mode) != 0U) &&
        (cmd_only_delimiters(line, len, (uint8_t)(i + 1U)) != 0U)) {
        return 1U;
    }
    return cmd_mode_from_word(line, len, i, mode);
}

static uint8_t cmd_has_explicit_mode_prefix(const uint8_t *line, uint8_t len)
{
    uint8_t i = 0U;

    if ((line == 0) || (len == 0U)) {
        return 0U;
    }
    while ((i < len) && (cmd_is_delimiter(line[i]) != 0U)) {
        i++;
    }
    if ((i >= len) || (cmd_to_lower(line[i]) != 'm')) {
        return 0U;
    }
    if ((uint8_t)(i + 1U) >= len) {
        return 1U;
    }
    if ((line[i + 1U] >= '0') && (line[i + 1U] <= '9')) {
        return 1U;
    }
    if (cmd_is_delimiter(line[i + 1U]) != 0U) {
        return 1U;
    }
    return (((uint8_t)(i + 4U) <= len) &&
            (cmd_to_lower(line[i + 1U]) == 'o') &&
            (cmd_to_lower(line[i + 2U]) == 'd') &&
            (cmd_to_lower(line[i + 3U]) == 'e')) ? 1U : 0U;
}

static uint8_t cmd_starts_text(const uint8_t *line,
                               uint8_t len,
                               uint8_t start,
                               const char *text)
{
    uint8_t i = 0U;

    while (text[i] != '\0') {
        if (((uint8_t)(start + i) >= len) ||
            (cmd_to_lower(line[start + i]) != (uint8_t)text[i])) {
            return 0U;
        }
        i++;
    }
    return 1U;
}

static uint8_t cmd_starts_mode_word(const uint8_t *line, uint8_t len)
{
    static const char *const words[] = {
        "stand", "balance", "forward", "go", "backward", "back",
        "reverse", "stop", "park", "left", "right", "track",
        "tracking", "line", "square", "box", "shoot", "aim",
        "target", "vision", "task", "cal_laser"
    };
    uint8_t start = 0U;

    if ((line == 0) || (len == 0U)) {
        return 0U;
    }
    while ((start < len) && (cmd_is_delimiter(line[start]) != 0U)) {
        start++;
    }
    if (start >= len) {
        return 0U;
    }

    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(words) / sizeof(words[0]));
         i++) {
        if (cmd_starts_text(line, len, start, words[i]) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t cmd_parse_task(const uint8_t *line,
                              uint8_t len,
                              uint8_t *task_id)
{
    uint8_t start = 0U;

    if ((line == 0) || (task_id == 0)) {
        return 0U;
    }
    while ((start < len) && (cmd_is_delimiter(line[start]) != 0U)) {
        start++;
    }
    for (uint8_t id = 1U; id <= 5U; id++) {
        const char word[] = {'t', 'a', 's', 'k', (char)('0' + id), '\0'};

        if (cmd_match_word(line, len, start, word) != 0U) {
            *task_id = id;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t cmd_parse_calibration_laser(const uint8_t *line,
                                           uint8_t len,
                                           uint8_t *enabled)
{
    uint8_t start = 0U;

    if ((line == 0) || (enabled == 0)) {
        return 0U;
    }
    while ((start < len) && (cmd_is_delimiter(line[start]) != 0U)) {
        start++;
    }
    if (cmd_match_word(line, len, start, "cal_laser_on") != 0U) {
        *enabled = 1U;
        return 1U;
    }
    if (cmd_match_word(line, len, start, "cal_laser_off") != 0U) {
        *enabled = 0U;
        return 1U;
    }
    return 0U;
}

static uint8_t cmd_is_static_target_word(const uint8_t *line, uint8_t len)
{
    uint8_t start = 0U;

    while ((start < len) && (cmd_is_delimiter(line[start]) != 0U)) {
        start++;
    }
    return ((cmd_match_word(line, len, start, "aim") != 0U) ||
            (cmd_match_word(line, len, start, "target") != 0U)) ? 1U : 0U;
}

static uint8_t cmd_is_vision_word(const uint8_t *line, uint8_t len)
{
    uint8_t start = 0U;

    while ((start < len) && (cmd_is_delimiter(line[start]) != 0U)) {
        start++;
    }
    return cmd_match_word(line, len, start, "vision");
}

static uint8_t cmd_parse_pid(const uint8_t *line,
                             uint8_t len,
                             BalancePidRuntimeParam_e *param,
                             float *value)
{
    uint8_t i = 0U;
    uint8_t c0;
    uint8_t c1;

    if (line == 0 || param == 0 || value == 0 || len == 0U) {
        return 0U;
    }

    while ((i < len) && (cmd_is_delimiter(line[i]) != 0U)) {
        i++;
    }
    if (i >= len) {
        return 0U;
    }

    c0 = cmd_to_lower(line[i]);
    c1 = ((uint8_t)(i + 1U) < len) ? cmd_to_lower(line[i + 1U]) : 0U;

    if (c0 == 'k') {
        if (c1 == 'p') {
            *param = BALANCE_PID_RUNTIME_KP;
            i += 2U;
        } else if (c1 == 'i') {
            *param = BALANCE_PID_RUNTIME_KI;
            i += 2U;
        } else if (c1 == 'd') {
            *param = BALANCE_PID_RUNTIME_KD;
            i += 2U;
        } else {
            return 0U;
        }
    } else if (c0 == 'p') {
        *param = BALANCE_PID_RUNTIME_KP;
        i++;
    } else if (c0 == 'i') {
        *param = BALANCE_PID_RUNTIME_KI;
        i++;
    } else if (c0 == 'd') {
        *param = BALANCE_PID_RUNTIME_KD;
        i++;
    } else if ((c0 == 'o') || (c0 == 'l')) {
        *param = BALANCE_PID_RUNTIME_SPEED_LIMIT_RPM;
        i++;
        while ((i < len) &&
               ((cmd_to_lower(line[i]) >= 'a') &&
                (cmd_to_lower(line[i]) <= 'z'))) {
            i++;
        }
    } else if (c0 == 'c') {
        *param = BALANCE_PID_RUNTIME_CURRENT_LIMIT_A;
        i++;
        while ((i < len) &&
               ((cmd_to_lower(line[i]) >= 'a') &&
                (cmd_to_lower(line[i]) <= 'z'))) {
            i++;
        }
    } else if (c0 == 'r') {
        *param = BALANCE_PID_RUNTIME_CURRENT_SLEW_A_PER_SEC;
        i++;
        while ((i < len) &&
               ((cmd_to_lower(line[i]) >= 'a') &&
                (cmd_to_lower(line[i]) <= 'z'))) {
            i++;
        }
    } else if ((c0 == 's') &&
               ((uint8_t)(i + 4U) <= len) &&
               (cmd_to_lower(line[i + 1U]) == 'l') &&
               (cmd_to_lower(line[i + 2U]) == 'e') &&
               (cmd_to_lower(line[i + 3U]) == 'w')) {
        *param = BALANCE_PID_RUNTIME_CURRENT_SLEW_A_PER_SEC;
        i += 4U;
    } else {
        return 0U;
    }

    return cmd_parse_float(line + i, (uint8_t)(len - i), value);
}

uint8_t ChassisSerialCmd_HandleLine(const uint8_t *line, uint8_t len)
{
    float value;
    ChassisMode_e mode;
    BalancePidRuntimeParam_e pid_param;
    uint8_t explicit_mode_command;
    uint8_t task_id;
    uint8_t calibration_laser_enabled;

    chassis_serial_cmd_rx_count++;
    explicit_mode_command =
        (cmd_has_explicit_mode_prefix(line, len) != 0U) ||
        (cmd_starts_mode_word(line, len) != 0U);

    if (cmd_parse_calibration_laser(
            line, len, &calibration_laser_enabled) != 0U) {
        if (Gimbal_SetCalibrationLaserRequest(
                calibration_laser_enabled) == 0U) {
            return 0U;
        }
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = 21U;
        chassis_serial_cmd_last_value =
            (float)calibration_laser_enabled;
        return 1U;
    }

    if (cmd_parse_task(line, len, &task_id) != 0U) {
        if (Chassis_StartCompetitionTask(task_id) == 0U) {
            return 0U;
        }
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = 20U;
        chassis_serial_cmd_last_value = (float)task_id;
        return 1U;
    }

    if (cmd_parse_mode(line, len, &mode) != 0U) {
        Chassis_StopCompetitionTask();
        Gimbal_SetVisionLaserRequest(cmd_is_vision_word(line, len));
        if (mode == CHASSIS_MODE_TARGET_AIM) {
            uint8_t static_target =
                cmd_is_static_target_word(line, len);

            Mode_SetTargetDriveEnabled((static_target != 0U) ? 0U : 1U);
            if (static_target != 0U) {
                Mode_SetTargetVisionReady(0U);
            }
        }
        Chassis_SetControlMode(mode);
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = 1U;
        chassis_serial_cmd_last_value = (float)mode;
        return 1U;
    }
    if (explicit_mode_command != 0U) {
        return 0U;
    }
    if (cmd_parse_bias(line, len, &value) != 0U) {
        Balance_SetManualBiasDeg(value);
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = 2U;
        chassis_serial_cmd_last_value = value;
        return 1U;
    }
    if (cmd_parse_pid(line, len, &pid_param, &value) != 0U) {
        Balance_SetPidRuntimeParam(pid_param, value);
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = (uint8_t)(10U + (uint8_t)pid_param);
        chassis_serial_cmd_last_value = value;
        return 1U;
    }
    if (cmd_parse_float(line, len, &value) != 0U) {
        /* Reject echoed telemetry such as 80000 instead of silently
         * clamping it into a real wheel-speed command. */
        if ((value < -300.0f) || (value > 300.0f)) {
            return 0U;
        }
        WheelSpeed_SetTargetRpm(value);
        chassis_serial_cmd_ok_count++;
        chassis_serial_cmd_last_type = 3U;
        chassis_serial_cmd_last_value = value;
        return 1U;
    }

    return 0U;
}
