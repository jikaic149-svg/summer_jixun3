import argparse
import socket

from vision_target_link import command_text_allowed


ALIASES = {
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


def normalize_command(value):
    command = ALIASES.get(value.strip(), value.strip())
    if not command_text_allowed(command):
        raise ValueError("command is not in the safe control whitelist")
    try:
        payload = command.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("unknown non-ASCII command or Chinese alias") from exc
    if not payload or len(payload) > 31:
        raise ValueError("command must be 1..31 ASCII bytes")
    if any(value < 0x20 or value > 0x7E for value in payload):
        raise ValueError("command contains unsupported control characters")
    return payload + b"\n"


def main():
    parser = argparse.ArgumentParser(description="Send a command to MaixCAM2.")
    parser.add_argument(
        "command",
        help="task1..task5, stop, legacy debug command, or Chinese alias",
    )
    parser.add_argument(
        "--host",
        required=True,
        help="MaixCAM IP; the normal bridge discovers this automatically",
    )
    parser.add_argument("--port", type=int, default=5007)
    args = parser.parse_args()

    payload = normalize_command(args.command)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(payload, (args.host, args.port))
    finally:
        sock.close()
    print(
        "sent %s:%d  %s"
        % (args.host, args.port, payload.decode("ascii").strip())
    )


if __name__ == "__main__":
    main()
