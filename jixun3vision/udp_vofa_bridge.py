import argparse
import math
import select
import socket
from collections import namedtuple

from vision_target_link import command_text_allowed


COMMAND_ALIASES = {
    "停止": "stop",
    "停机": "stop",
    "任务1": "task1",
    "任务一": "task1",
    "任务2": "task2",
    "任务二": "task2",
    "任务3": "task3",
    "任务三": "task3",
    "任务4": "task4",
    "任务四": "task4",
    "任务5": "task5",
    "任务五": "task5",
    "站立": "stand",
    "前进": "forward",
    "后退": "backward",
    "左转": "left",
    "右转": "right",
    "循迹": "track",
}


VisionStatus = namedtuple(
    "VisionStatus",
    (
        "task",
        "phase",
        "shot",
        "locked",
        "geometry",
        "dx",
        "dy",
        "score",
        "quality",
        "milestones",
        "timestamp_ms",
    ),
)


def _is_ascii_token(value, max_length=31):
    if not value or len(value) > max_length:
        return False
    return all(0x21 <= ord(char) <= 0x7E and char != "," for char in value)


def parse_target_payload(data):
    try:
        text = data.decode("ascii").strip()
        parts = text.split(",")
        if len(parts) != 3:
            return None
        dx = int(parts[0])
        dy = int(parts[1])
        valid = int(parts[2])
        if valid not in (0, 1):
            return None
        return dx, dy, valid
    except (UnicodeDecodeError, ValueError):
        return None


def firewater_payload(sample):
    return ("%d,%d,%d\n" % sample).encode("ascii")


def parse_firewater_payload(data, max_channels=32):
    try:
        text = data.decode("ascii").strip()
        if not text or "\n" in text or "\r" in text:
            return None
        fields = text.split(",")
        if not fields or len(fields) > max_channels:
            return None
        values = tuple(float(field.strip()) for field in fields)
        if not all(math.isfinite(value) for value in values):
            return None
        return values, (text + "\n").encode("ascii")
    except (UnicodeDecodeError, ValueError):
        return None


def parse_command_payload(data, max_length=31):
    try:
        text = data.decode("utf-8").strip()
    except UnicodeDecodeError:
        return None
    if not text or "\n" in text or "\r" in text:
        return None

    text = COMMAND_ALIASES.get(text, text)
    if not command_text_allowed(text):
        return None
    try:
        payload = text.encode("ascii")
    except UnicodeEncodeError:
        return None
    if len(payload) > max_length:
        return None
    if any(value < 0x20 or value > 0x7E for value in payload):
        return None
    return payload + b"\n"


def parse_vision_status(data, max_length=256):
    """Parse and validate one MaixCAM VISION status datagram."""
    if not data or len(data) > max_length:
        return None
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        return None

    if text.endswith("\n"):
        text = text[:-1]
    if text.endswith("\r"):
        text = text[:-1]
    if not text or "\n" in text or "\r" in text:
        return None

    fields = text.split(",")
    if len(fields) != 12 or fields[0] != "VISION":
        return None
    if not _is_ascii_token(fields[2]) or not _is_ascii_token(fields[5]):
        return None

    try:
        task = int(fields[1])
        shot = int(fields[3])
        locked = int(fields[4])
        dx = int(fields[6])
        dy = int(fields[7])
        score = int(fields[8])
        quality = float(fields[9])
        milestones = int(fields[10])
        timestamp_ms = int(fields[11])
    except ValueError:
        return None

    if not 0 <= task <= 5:
        return None
    if not 0 <= shot <= 2:
        return None
    if locked not in (0, 1):
        return None
    if not -32768 <= dx <= 32767 or not -32768 <= dy <= 32767:
        return None
    if not -1 <= score <= 10:
        return None
    if not math.isfinite(quality) or not 0.0 <= quality <= 1.0:
        return None
    if not 0 <= milestones <= 0x0F:
        return None
    if not 0 <= timestamp_ms <= 0xFFFFFFFFFFFFFFFF:
        return None

    return VisionStatus(
        task,
        fields[2],
        shot,
        locked,
        fields[5],
        dx,
        dy,
        score,
        quality,
        milestones,
        timestamp_ms,
    )


def format_vision_status(status):
    lock_text = "LOCK" if status.locked else "LOST"
    score_text = "unknown" if status.score < 0 else str(status.score)
    return (
        "VISION task=%d phase=%s shot=%d %s geometry=%s "
        "dx=%d dy=%d score=%s quality=%.2f milestones=0x%X time=%d"
        % (
            status.task,
            status.phase,
            status.shot,
            lock_text,
            status.geometry,
            status.dx,
            status.dy,
            score_text,
            status.quality,
            status.milestones,
            status.timestamp_ms,
        )
    )


class UdpBridgeRouter:
    """Route UDP payloads and learn the MaixCAM command endpoint."""

    def __init__(
        self,
        vofa_host,
        vofa_port,
        command_host=None,
        command_port=5007,
    ):
        self.vofa_host = vofa_host
        self.vofa_port = int(vofa_port)
        self.command_host = command_host
        self.command_port = int(command_port)
        self.discovered_command_host = None

    @property
    def command_endpoint(self):
        host = self.command_host or self.discovered_command_host
        if not host:
            return None
        return host, self.command_port

    def route(self, data, address):
        if address[0] == self.vofa_host and address[1] == self.vofa_port:
            command = parse_command_payload(data)
            if command is None:
                return "invalid_command", None, None, None
            endpoint = self.command_endpoint
            if endpoint is None:
                return "command_unavailable", command, None, None
            return "command", command, endpoint, None

        parsed = parse_firewater_payload(data)
        if parsed is None:
            return "invalid_telemetry", None, None, None

        values, payload = parsed
        if self.command_host is None:
            self.discovered_command_host = address[0]
        return (
            "telemetry",
            payload,
            (self.vofa_host, self.vofa_port),
            values,
        )

    def route_vision_status(self, data, address):
        status = parse_vision_status(data)
        if status is None:
            return "invalid_vision", None
        if self.command_host is None:
            self.discovered_command_host = address[0]
        return "vision", status


def run_bridge(
    listen_host,
    listen_port,
    vofa_host,
    vofa_port,
    quiet=False,
    max_packets=0,
    command_host=None,
    command_port=5007,
    status_port=5008,
    ready_event=None,
):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    receiver.bind((listen_host, listen_port))

    status_receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    status_receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        status_receiver.bind((listen_host, status_port))
    except Exception:
        status_receiver.close()
        receiver.close()
        raise

    router = UdpBridgeRouter(
        vofa_host,
        vofa_port,
        command_host=command_host,
        command_port=command_port,
    )

    if not quiet:
        print("MaixCAM UDP: %s:%d" % (listen_host, listen_port))
        print("VISION UDP:  %s:%d" % (listen_host, status_port))
        print("VOFA+ UDP:   %s:%d (FireWater)" % (vofa_host, vofa_port))
        if command_host:
            print("Command UDP: %s:%d (fixed)" % (command_host, command_port))
        else:
            print("Command UDP: auto-discovery from valid MaixCAM packets")
    if ready_event is not None:
        ready_event.set()

    handled = 0
    try:
        while max_packets <= 0 or handled < max_packets:
            try:
                readable, _, _ = select.select(
                    (receiver, status_receiver), (), ()
                )
                active_receiver = readable[0]
                data, address = active_receiver.recvfrom(2048)
            except ConnectionResetError:
                # Windows reports ICMP "port unreachable" from an earlier
                # UDP send on the next recvfrom(). Keep both listeners alive.
                if not quiet:
                    print("UDP peer is not listening yet; retrying")
                continue

            if active_receiver is status_receiver:
                previous_endpoint = router.command_endpoint
                kind, status = router.route_vision_status(data, address)
                if kind == "invalid_vision":
                    if not quiet:
                        print(
                            "drop %s:%d invalid VISION=%r"
                            % (address[0], address[1], data[:120])
                        )
                    continue
                handled += 1
                if previous_endpoint != router.command_endpoint and not quiet:
                    print(
                        "MaixCAM command discovered: %s:%d"
                        % router.command_endpoint
                    )
                if not quiet:
                    print(
                        "%s:%d  %s"
                        % (
                            address[0],
                            address[1],
                            format_vision_status(status),
                        )
                    )
                continue

            previous_endpoint = router.command_endpoint
            kind, payload, target, values = router.route(data, address)

            if kind == "invalid_command":
                if not quiet:
                    print("drop VOFA command invalid=%r" % data[:80])
                continue
            if kind == "command_unavailable":
                if not quiet:
                    print("drop VOFA command: waiting for MaixCAM telemetry")
                continue
            if kind == "command":
                try:
                    receiver.sendto(payload, target)
                except OSError as error:
                    if not quiet:
                        print("command forward failed: %s" % error)
                    continue
                handled += 1
                if not quiet:
                    print(
                        "command -> %s:%d  %s"
                        % (
                            target[0],
                            target[1],
                            payload.decode("ascii").strip(),
                        )
                    )
                continue

            if kind == "invalid_telemetry":
                if not quiet:
                    print(
                        "drop %s:%d invalid=%r"
                        % (address[0], address[1], data[:80])
                    )
                continue

            if previous_endpoint != router.command_endpoint and not quiet:
                print("MaixCAM command discovered: %s:%d" % router.command_endpoint)
            # Reuse the listening socket so VOFA+ sees a stable source port.
            try:
                receiver.sendto(payload, target)
            except OSError as error:
                if not quiet:
                    print("VOFA+ forward failed: %s" % error)
                continue
            handled += 1
            if not quiet:
                target_sample = parse_target_payload(payload)
                if target_sample is not None:
                    print(
                        "%s:%d  dx=%d dy=%d valid=%d"
                        % (
                            address[0],
                            address[1],
                            target_sample[0],
                            target_sample[1],
                            target_sample[2],
                        )
                    )
                else:
                    print(
                        "%s:%d  channels=%d  %s"
                        % (
                            address[0],
                            address[1],
                            len(values),
                            payload.decode("ascii").strip(),
                        )
                    )
    finally:
        status_receiver.close()
        receiver.close()


def build_parser():
    parser = argparse.ArgumentParser(
        description=(
            "Forward MaixCAM telemetry to VOFA+ and display VISION status."
        )
    )
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=5005)
    parser.add_argument("--vofa-host", default="127.0.0.1")
    parser.add_argument("--vofa-port", type=int, default=5006)
    parser.add_argument(
        "--command-host",
        default=None,
        help="fixed MaixCAM host; default learns it from valid packets",
    )
    parser.add_argument("--command-port", type=int, default=5007)
    parser.add_argument("--status-port", type=int, default=5008)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--max-packets", type=int, default=0, help=argparse.SUPPRESS)
    return parser


def main():
    args = build_parser().parse_args()
    run_bridge(
        args.listen_host,
        args.listen_port,
        args.vofa_host,
        args.vofa_port,
        args.quiet,
        args.max_packets,
        args.command_host,
        args.command_port,
        args.status_port,
    )


if __name__ == "__main__":
    main()
