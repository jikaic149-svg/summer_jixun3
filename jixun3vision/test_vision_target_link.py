import socket
import sys
import types
import unittest
from unittest.mock import patch

from vision_target_link import TargetOutputLimiter, VisionTargetLink


class FakePort:
    def __init__(self):
        self.writes = []

    def write(self, payload):
        self.writes.append(payload)


class FakeSocket:
    def __init__(self):
        self.options = []
        self.bound = None
        self.blocking = None
        self.close_count = 0

    def setsockopt(self, level, option, value):
        self.options.append((level, option, value))

    def bind(self, address):
        self.bound = address

    def setblocking(self, enabled):
        self.blocking = enabled

    def close(self):
        self.close_count += 1


class BufferedPort:
    def __init__(self, data=b"", events=None):
        self.data = bytearray(data)
        self.events = events if events is not None else []
        self.writes = []
        self.read_sizes = []
        self.closed = False

    def available(self):
        return len(self.data)

    def read(self, size, _timeout=0):
        self.events.append("read")
        self.read_sizes.append(size)
        chunk = bytes(self.data[:size])
        del self.data[:size]
        return chunk

    def write(self, payload):
        self.events.append("write:" + payload.decode("ascii").strip())
        self.writes.append(payload)
        return len(payload)

    def close(self):
        self.closed = True


class CommandSocket:
    def __init__(self, packets, events=None):
        self.packets = list(packets)
        self.events = events if events is not None else []
        self.close_count = 0

    def recvfrom(self, _size):
        if not self.packets:
            raise BlockingIOError()
        self.events.append("command")
        return self.packets.pop(0)

    def close(self):
        self.close_count += 1


class TargetOutputLimiterTest(unittest.TestCase):
    def test_one_pixel_error_is_not_removed(self):
        limiter = TargetOutputLimiter(yaw_only=True)
        self.assertEqual(limiter.update(1, 99, True), (1, 0, 1))

    def test_full_frame_direction_change_is_not_stepped(self):
        limiter = TargetOutputLimiter(yaw_only=True)
        self.assertEqual(limiter.update(208, 0, True), (208, 0, 1))
        self.assertEqual(limiter.update(-208, 0, True), (-208, 0, 1))

    def test_invalid_target_is_zero_and_invalid(self):
        limiter = TargetOutputLimiter(yaw_only=True)
        limiter.update(200, 100, True)
        self.assertEqual(limiter.update(0, 0, False), (0, 0, 0))

    def test_uart_packet_keeps_exact_yaw_error(self):
        link = VisionTargetLink(period_ms=0, yaw_only=True)
        link.port = FakePort()
        link.available = True
        self.assertTrue(link.send_error(-11, 55, True, 100))
        self.assertEqual(link.port.writes, [b"-11,0,1\n"])

    def test_udp_starts_with_broadcast_when_uart_init_fails(self):
        class FailingPinmap:
            @staticmethod
            def set_pin_function(_pin, _function):
                raise RuntimeError("UART unavailable")

        fake_maix = types.ModuleType("maix")
        fake_maix.pinmap = FailingPinmap()
        fake_maix.uart = object()
        tx_socket = FakeSocket()
        rx_socket = FakeSocket()

        with patch.dict(sys.modules, {"maix": fake_maix}):
            with patch("socket.socket", side_effect=(tx_socket, rx_socket)):
                link = VisionTargetLink(
                    telemetry_host="255.255.255.255",
                    command_port=5007,
                )
                self.assertTrue(link.start())

        self.assertFalse(link.available)
        self.assertIs(link.telemetry_sock, tx_socket)
        self.assertIs(link.command_sock, rx_socket)
        self.assertIsNot(link.telemetry_sock, link.command_sock)
        self.assertIsNone(tx_socket.bound)
        self.assertEqual(rx_socket.bound, ("0.0.0.0", 5007))
        self.assertFalse(tx_socket.blocking)
        self.assertFalse(rx_socket.blocking)
        self.assertIn(
            (socket.SOL_SOCKET, socket.SO_BROADCAST, 1),
            tx_socket.options,
        )
        self.assertIn(
            (socket.SOL_SOCKET, socket.SO_REUSEADDR, 1),
            rx_socket.options,
        )

    def test_poll_handles_stop_before_bounded_uart_backlog(self):
        events = []
        port = BufferedPort(b"1,2,3\n" * 200, events)
        commands = CommandSocket(
            [(b"stop", ("192.168.237.147", 6000))], events,
        )
        link = VisionTargetLink(
            telemetry_host="192.168.237.147",
            command_host="192.168.237.147",
        )
        link.port = port
        link.available = True
        link.command_sock = commands

        link.poll()

        self.assertEqual(events[:2], ["command", "write:stop"])
        self.assertIn("read", events)
        self.assertEqual(port.read_sizes, [256])
        self.assertGreater(len(port.data), 0)

    def test_uart_short_write_is_reported(self):
        class ShortPort:
            def write(self, payload):
                return len(payload) - 1

        link = VisionTargetLink(period_ms=0)
        link.port = ShortPort()
        link.available = True
        self.assertFalse(link.send_vision(1, 2, True, True, False, 0, 1))
        self.assertIn("short write", link.last_error)

    def test_invalidate_vision_does_not_send_stop_command(self):
        link = VisionTargetLink(period_ms=1000)
        link.port = FakePort()
        link.available = True

        self.assertTrue(link.invalidate_vision(100, move_epoch=9, force=True))

        self.assertEqual(link.port.writes, [b"V,0,0,0,0,0,9\n"])

    def test_safe_stop_still_invalidates_then_stops(self):
        link = VisionTargetLink(period_ms=1000)
        link.port = FakePort()
        link.available = True

        self.assertTrue(link.safe_stop(100))

        self.assertEqual(
            link.port.writes,
            [b"V,0,0,0,0,0,0\n", b"stop\n"],
        )

    def test_overlong_uart_line_is_discarded_until_newline(self):
        port = BufferedPort(b"9" * 520 + b"\n1,2,3\n")
        link = VisionTargetLink()
        link.port = port
        link.available = True
        link.uart_read_budget = 600
        link.telemetry_sock = None

        self.assertEqual(link.poll(), 1)
        self.assertEqual(link.last_status, "1,2,3")
        self.assertFalse(link.rx_discarding)
        self.assertEqual(link.telemetry_drop_count, 2)

    def test_close_releases_shared_socket_once(self):
        port = BufferedPort()
        sock = CommandSocket([])
        link = VisionTargetLink()
        link.port = port
        link.command_sock = sock
        link.telemetry_sock = sock
        link.available = True
        link.udp_available = True

        link.close()
        link.close()

        self.assertTrue(port.closed)
        self.assertEqual(sock.close_count, 1)
        self.assertFalse(link.available)
        self.assertFalse(link.udp_available)


if __name__ == "__main__":
    unittest.main()
