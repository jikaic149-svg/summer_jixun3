"""UART target protocol and bidirectional UDP bridge."""


class TargetOutputLimiter:
    def __init__(
        self,
        max_step_x=2048,
        max_step_y=2048,
        max_abs_x=2048,
        max_abs_y=2048,
        scale_x=1.0,
        scale_y=1.0,
        polarity_x=1,
        polarity_y=1,
        deadband_x=0,
        deadband_y=0,
        yaw_only=True,
    ):
        self.max_step_x = int(max_step_x)
        self.max_step_y = int(max_step_y)
        self.max_abs_x = int(max_abs_x)
        self.max_abs_y = int(max_abs_y)
        self.scale_x = float(scale_x)
        self.scale_y = float(scale_y)
        self.polarity_x = 1 if polarity_x >= 0 else -1
        self.polarity_y = 1 if polarity_y >= 0 else -1
        self.deadband_x = int(deadband_x)
        self.deadband_y = int(deadband_y)
        self.yaw_only = bool(yaw_only)
        self.dx = 0
        self.dy = 0

    @staticmethod
    def _clamp(value, limit):
        return max(-limit, min(limit, value))

    @staticmethod
    def _limit_step(current, target, max_step):
        delta = target - current
        if delta > max_step:
            return current + max_step
        if delta < -max_step:
            return current - max_step
        return target

    def reset(self):
        self.dx = 0
        self.dy = 0

    def update(self, raw_dx, raw_dy, valid):
        if valid:
            raw_dx = 0 if abs(int(raw_dx)) <= self.deadband_x else int(raw_dx)
            raw_dy = 0 if abs(int(raw_dy)) <= self.deadband_y else int(raw_dy)
            target_dx = int(raw_dx * self.scale_x * self.polarity_x)
            target_dy = int(raw_dy * self.scale_y * self.polarity_y)
            target_dx = self._clamp(target_dx, self.max_abs_x)
            target_dy = self._clamp(target_dy, self.max_abs_y)
        else:
            target_dx = 0
            target_dy = 0
        if self.yaw_only:
            target_dy = 0
        self.dx = self._limit_step(self.dx, target_dx, self.max_step_x)
        self.dy = self._limit_step(self.dy, target_dy, self.max_step_y)
        return self.dx, self.dy, 1 if valid else 0


class TaskStatus:
    def __init__(self):
        self.task_id = 0
        self.phase = "IDLE"
        self.shot_index = 0
        self.aim_ready = False
        self.laser = False
        self.milestones = 0
        self.done = False
        self.valid = False


TASK_PHASE_NAMES = (
    "IDLE",
    "TRACK_CENTER",
    "AIM_ACQUIRE",
    "LASER_DWELL",
    "WAIT_MOVE",
    "TRACK_FINISH",
    "COMPLETE",
    "STOPPED",
)

SAFE_EXACT_COMMANDS = (
    "task1", "task2", "task3", "task4", "task5", "stop",
    "cal_laser_on", "cal_laser_off",
    "stand", "balance", "vision", "forward", "go", "backward",
    "back", "reverse", "park", "left", "right", "track",
    "tracking", "line", "square", "box", "aim", "shoot", "target",
)
TUNING_COMMAND_PREFIXES = (
    "bias", "kp", "ki", "kd", "slew", "output", "limit",
    "current", "ramp", "b", "p", "i", "d", "o", "l", "c", "r",
)
SAFE_MODE_ARGUMENTS = SAFE_EXACT_COMMANDS + (
    "0", "1", "2", "3", "4", "5", "6", "7",
)


def command_text_allowed(text):
    """Reject bare numeric feedback while preserving named safe commands."""
    lower = str(text).strip().lower()
    if lower in SAFE_EXACT_COMMANDS:
        return True
    if lower.startswith("mode"):
        return lower[4:].strip(" ,:=") in SAFE_MODE_ARGUMENTS
    if lower.startswith("m"):
        if lower[1:].strip(" ,:=") in SAFE_MODE_ARGUMENTS:
            return True
    for prefix in TUNING_COMMAND_PREFIXES:
        if lower.startswith(prefix):
            tail = lower[len(prefix):].strip(" ,:=")
            try:
                value = float(tail)
                return value == value and abs(value) != float("inf")
            except ValueError:
                return False
    return False


def parse_task_status(line):
    try:
        text = line.decode("ascii") if isinstance(line, (bytes, bytearray)) else str(line)
        fields = text.strip().split(",")
        if len(fields) != 8 or fields[0] != "T":
            return None
        status = TaskStatus()
        status.task_id = int(fields[1])
        phase_token = fields[2].strip()
        try:
            phase_code = int(phase_token)
            status.phase = (
                TASK_PHASE_NAMES[phase_code]
                if 0 <= phase_code < len(TASK_PHASE_NAMES)
                else "PHASE_%d" % phase_code
            )
        except ValueError:
            status.phase = phase_token or "UNKNOWN"
        status.shot_index = int(fields[3])
        status.aim_ready = bool(int(fields[4]))
        status.laser = bool(int(fields[5]))
        status.milestones = int(fields[6], 0)
        status.done = bool(int(fields[7]))
        status.valid = True
        return status
    except Exception:
        return None


def parse_vision_frame(line):
    try:
        text = line.decode("ascii") if isinstance(line, (bytes, bytearray)) else str(line)
        fields = text.strip().split(",")
        if len(fields) == 3:
            return {
                "dx": int(fields[0]), "dy": int(fields[1]),
                "valid": bool(int(fields[2])), "locked": bool(int(fields[2])),
                "aim_ready": False, "move_epoch": 0, "legacy": True,
            }
        if len(fields) == 7 and fields[0] == "V":
            return {
                "dx": int(fields[1]), "dy": int(fields[2]),
                "valid": bool(int(fields[3])), "locked": bool(int(fields[4])),
                "aim_ready": bool(int(fields[5])), "move_epoch": int(fields[6]),
                "legacy": False,
            }
    except Exception:
        pass
    return None


def format_vision_frame(dx, dy, valid, locked=False, aim_ready=False, move_epoch=0):
    if not valid:
        dx = 0
        dy = 0
        locked = False
        aim_ready = False
    return "V,%d,%d,%d,%d,%d,%d\n" % (
        int(dx), int(dy), 1 if valid else 0, 1 if locked else 0,
        1 if aim_ready else 0, max(0, int(move_epoch)),
    )


def build_status_payload(
    task, phase, shot, locked, geometry, dx, dy, score, quality,
    milestones, timestamp_ms,
):
    phase = str(phase).replace(",", "_").replace("\n", "_")[:24] or "UNKNOWN"
    geometry = str(geometry).replace(",", "_").replace("\n", "_")[:12] or "none"
    return (
        "VISION,%d,%s,%d,%d,%s,%d,%d,%d,%.3f,%d,%d\n" % (
            int(task), phase, int(shot), 1 if locked else 0, geometry,
            int(dx), int(dy), int(score), max(0.0, min(1.0, float(quality))),
            int(milestones), int(timestamp_ms),
        )
    ).encode("ascii")


class UdpVisionTargetLink:
    """Backward-compatible old UDP target sender."""

    def __init__(self, host="192.168.237.147", port=5005, period_ms=15, yaw_only=True,
                 polarity_x=1, polarity_y=1):
        self.host = host
        self.port = int(port)
        self.period_ms = int(period_ms)
        self.sock = None
        self.available = False
        self.last_error = ""
        self.last_tx_ms = -self.period_ms
        self.last_tx = (0, 0, 0)
        self.tx_count = 0
        self.limiter = TargetOutputLimiter(
            yaw_only=yaw_only, polarity_x=polarity_x, polarity_y=polarity_y,
        )

    def start(self):
        try:
            import socket
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            if self.host == "255.255.255.255" or self.host.endswith(".255"):
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.available = True
            return True
        except Exception as error:
            self.last_error = str(error)
            return False

    def poll(self):
        return 0

    def send_error(self, raw_dx, raw_dy, valid, now_ms):
        if int(now_ms) - self.last_tx_ms < self.period_ms:
            return False
        self.last_tx_ms = int(now_ms)
        self.last_tx = self.limiter.update(raw_dx, raw_dy, valid)
        if not self.available or self.sock is None:
            return False
        try:
            payload = ("%d,%d,%d\n" % self.last_tx).encode("ascii")
            self.sock.sendto(payload, (self.host, self.port))
            self.tx_count += 1
            self.last_error = ""
            return True
        except Exception as error:
            self.last_error = str(error)
            return False

    def send_target(self, target_x, target_y, aim_x, aim_y, now_ms):
        return self.send_error(target_x - aim_x, target_y - aim_y, True, now_ms)

    def send_lost(self, now_ms):
        return self.send_error(0, 0, False, now_ms)


class VisionTargetLink:
    """UART V/T protocol plus UDP 5005/5007/5008 transport."""

    def __init__(
        self,
        device="/dev/ttyS2",
        baud=115200,
        period_ms=40,
        yaw_only=True,
        polarity_x=1,
        polarity_y=1,
        telemetry_host=None,
        telemetry_port=5005,
        command_port=5007,
        status_port=5008,
        command_host=None,
    ):
        self.device = device
        self.baud = int(baud)
        self.period_ms = int(period_ms)
        self.telemetry_host = telemetry_host
        self.telemetry_port = int(telemetry_port)
        self.command_port = int(command_port)
        self.status_port = int(status_port)
        self.command_host = command_host or telemetry_host
        self.port = None
        self.telemetry_sock = None
        self.command_sock = None
        self.available = False
        self.udp_available = False
        self.last_error = ""
        self.telemetry_last_error = ""
        self.command_last_error = ""
        self.last_tx_ms = -self.period_ms
        self.last_status_tx_ms = -1000
        self.last_tx = (0, 0, 0)
        self.tx_count = 0
        self.rx_count = 0
        self.rx_line_count = 0
        self.telemetry_tx_count = 0
        self.telemetry_drop_count = 0
        self.status_tx_count = 0
        self.command_rx_count = 0
        self.command_uart_tx_count = 0
        self.command_drop_count = 0
        self.last_command = ""
        self.last_status = ""
        self.task_status = TaskStatus()
        self.rx_line = bytearray()
        self.rx_discarding = False
        self.uart_read_budget = 256
        self.command_poll_budget = 2
        self.limiter = TargetOutputLimiter(
            yaw_only=yaw_only,
            polarity_x=polarity_x,
            polarity_y=polarity_y,
        )

    def start(self):
        uart_ready = False
        try:
            from maix import pinmap, uart
            pinmap.set_pin_function("B0", "UART2_TX")
            pinmap.set_pin_function("B1", "UART2_RX")
            self.port = uart.UART(self.device, self.baud)
            self.available = True
            uart_ready = True
            self.last_error = ""
            print("target UART ready: %s %d" % (self.device, self.baud))
        except Exception as error:
            self.available = False
            self.last_error = str(error)
            print("target UART init failed:", error)

        udp_ready = False
        if self.telemetry_host:
            tx_sock = None
            rx_sock = None
            try:
                import socket
                # Keep send and receive on separate native sockets. Mixing
                # nonblocking recvfrom and sendto on one bound Maix socket has
                # caused process-level instability on some firmware builds.
                tx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                tx_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                tx_sock.setblocking(False)
                rx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                rx_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                rx_sock.bind(("0.0.0.0", self.command_port))
                rx_sock.setblocking(False)
                self.telemetry_sock = tx_sock
                self.command_sock = rx_sock
                self.udp_available = True
                udp_ready = True
                self.telemetry_last_error = ""
                print("UDP telemetry/status -> %s:%d/%d; commands :%d" % (
                    self.telemetry_host, self.telemetry_port,
                    self.status_port, self.command_port,
                ))
            except Exception as error:
                for sock in (rx_sock, tx_sock):
                    if sock is not None:
                        try:
                            sock.close()
                        except Exception:
                            pass
                self.telemetry_sock = None
                self.command_sock = None
                self.udp_available = False
                self.telemetry_last_error = str(error)
                print("UDP init failed:", error)
        return uart_ready or udp_ready

    @staticmethod
    def _is_numeric_csv(line):
        try:
            text = line.decode("ascii").strip()
            fields = text.split(",")
            if not text or not fields or len(fields) > 32:
                return False
            for field in fields:
                value = float(field.strip())
                if value != value or abs(value) == float("inf"):
                    return False
            return True
        except Exception:
            return False

    @staticmethod
    def _command_allowed(text):
        return command_text_allowed(text)

    @classmethod
    def _normalize_command(cls, data):
        try:
            text = data.decode("ascii").strip()
        except Exception:
            return None
        if not text or len(text) > 31 or not cls._command_allowed(text):
            return None
        return text.encode("ascii") + b"\n"

    def _forward_telemetry(self, line):
        if not self._is_numeric_csv(line) or self.telemetry_sock is None:
            self.telemetry_drop_count += 1
            return False
        try:
            self.telemetry_sock.sendto(
                bytes(line) + b"\n", (self.telemetry_host, self.telemetry_port)
            )
            self.telemetry_tx_count += 1
            self.telemetry_last_error = ""
            return True
        except Exception as error:
            self.telemetry_last_error = str(error)
            self.telemetry_drop_count += 1
            return False

    def _consume_uart_line(self, line):
        parsed = parse_task_status(line)
        if parsed is not None:
            self.task_status = parsed
            return "task"
        if self._forward_telemetry(line):
            return "telemetry"
        return "drop"

    def _poll_commands(self):
        if self.command_sock is None or self.port is None:
            return 0
        forwarded = 0
        for _index in range(self.command_poll_budget):
            try:
                data, address = self.command_sock.recvfrom(128)
            except (BlockingIOError, OSError) as error:
                if getattr(error, "errno", None) in (None, 11, 35, 10035):
                    break
                self.command_last_error = str(error)
                break
            except Exception as error:
                self.command_last_error = str(error)
                break
            self.command_rx_count += 1
            if (
                self.command_host and self.command_host != "255.255.255.255" and
                not self.command_host.endswith(".255") and address[0] != self.command_host
            ):
                self.command_drop_count += 1
                self.command_last_error = "command source rejected"
                continue
            payload = self._normalize_command(data)
            if payload is None:
                self.command_drop_count += 1
                self.command_last_error = "command rejected"
                continue
            try:
                written = self.port.write(payload)
                if written is not None and int(written) != len(payload):
                    raise OSError("UART command short write %s/%s" % (
                        written, len(payload),
                    ))
                self.command_uart_tx_count += 1
                self.last_command = payload.decode("ascii").strip()
                self.command_last_error = ""
                forwarded += 1
            except Exception as error:
                self.command_drop_count += 1
                self.command_last_error = str(error)
        return forwarded

    def poll(self):
        completed = 0
        # Commands, especially STOP, must not wait behind accumulated STM32
        # telemetry processing.
        self._poll_commands()
        if self.available and self.port is not None:
            try:
                try:
                    data = self.port.read(self.uart_read_budget)
                except TypeError:
                    data = self.port.read()
                if data:
                    raw = bytes(data)
                    self.rx_count += len(raw)
                    for value in raw:
                        if value == 10:
                            if self.rx_discarding:
                                self.rx_discarding = False
                                self.rx_line = bytearray()
                                continue
                            line = bytes(self.rx_line).rstrip(b"\r")
                            self.rx_line = bytearray()
                            if line:
                                completed += 1
                                self.rx_line_count += 1
                                self.last_status = line.decode("ascii", "ignore").strip()
                                self._consume_uart_line(line)
                        elif value != 13:
                            if self.rx_discarding:
                                continue
                            if len(self.rx_line) < 511:
                                self.rx_line.append(value)
                            else:
                                self.rx_line = bytearray()
                                self.rx_discarding = True
                                self.telemetry_drop_count += 1
            except Exception as error:
                self.last_error = str(error)
        return completed

    def _write(self, payload):
        if not self.available or self.port is None:
            return False
        try:
            written = self.port.write(payload)
            if written is not None and int(written) != len(payload):
                raise OSError("UART short write %s/%s" % (written, len(payload)))
            self.tx_count += 1
            self.last_error = ""
            return True
        except Exception as error:
            self.last_error = str(error)
            return False

    def send_error(self, raw_dx, raw_dy, valid, now_ms):
        """Send the legacy dx,dy,valid packet for old MCU compatibility."""
        if int(now_ms) - self.last_tx_ms < self.period_ms:
            return False
        self.last_tx_ms = int(now_ms)
        self.last_tx = self.limiter.update(raw_dx, raw_dy, valid)
        return self._write(("%d,%d,%d\n" % self.last_tx).encode("ascii"))

    def send_vision(self, raw_dx, raw_dy, valid, locked, aim_ready, move_epoch, now_ms,
                    force=False):
        if not force and int(now_ms) - self.last_tx_ms < self.period_ms:
            return False
        self.last_tx_ms = int(now_ms)
        dx, dy, valid_flag = self.limiter.update(raw_dx, raw_dy, valid)
        self.last_tx = (dx, dy, valid_flag)
        payload = format_vision_frame(
            dx, dy, bool(valid_flag), locked, aim_ready, move_epoch,
        ).encode("ascii")
        return self._write(payload)

    def send_target(self, target_x, target_y, aim_x, aim_y, now_ms):
        return self.send_error(target_x - aim_x, target_y - aim_y, True, now_ms)

    def send_lost(self, now_ms):
        return self.send_error(0, 0, False, now_ms)

    def send_command(self, command):
        payload = self._normalize_command(str(command).encode("ascii"))
        if payload is None:
            return False
        return self._write(payload)

    def clear_task_status(self, task_id=0):
        self.task_status = TaskStatus()
        self.task_status.task_id = int(task_id)
        self.task_status.phase = "STARTING" if task_id else "IDLE"

    def invalidate_vision(self, now_ms, move_epoch=0, force=True):
        """Revoke visual control without stopping the active MCU task."""
        return self.send_vision(
            0, 0, False, False, False, move_epoch, now_ms, force=force,
        )

    def safe_stop(self, now_ms):
        invalid_ok = self.invalidate_vision(now_ms, force=True)
        stop_ok = self.send_command("stop")
        return invalid_ok and stop_ok

    def send_status(self, payload):
        if self.telemetry_sock is None or not self.telemetry_host:
            return False
        try:
            self.telemetry_sock.sendto(bytes(payload), (self.telemetry_host, self.status_port))
            self.status_tx_count += 1
            self.telemetry_last_error = ""
            return True
        except Exception as error:
            self.telemetry_last_error = str(error)
            return False

    def close(self):
        """Idempotently release UART and UDP resources for IDE reruns."""
        sockets = []
        for sock in (self.command_sock, self.telemetry_sock):
            if sock is not None and sock not in sockets:
                sockets.append(sock)
        self.command_sock = None
        self.telemetry_sock = None
        for sock in sockets:
            try:
                sock.close()
            except Exception:
                pass
        if self.port is not None:
            try:
                self.port.close()
            except Exception:
                pass
        self.port = None
        self.available = False
        self.udp_available = False

    @property
    def combined_error(self):
        return self.last_error or self.telemetry_last_error or self.command_last_error
