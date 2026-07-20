import socket
import threading
import unittest

from send_udp_command import normalize_command
from udp_vofa_bridge import (
    UdpBridgeRouter,
    build_parser,
    format_vision_status,
    parse_vision_status,
    run_bridge,
)


class UdpBridgeRouterTest(unittest.TestCase):
    def test_valid_telemetry_discovers_maix_command_endpoint(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)

        kind, payload, target, values = router.route(
            b"1.5,-2,3\n",
            ("192.168.1.66", 41234),
        )

        self.assertEqual(kind, "telemetry")
        self.assertEqual(payload, b"1.5,-2,3\n")
        self.assertEqual(target, ("127.0.0.1", 5006))
        self.assertEqual(values, (1.5, -2.0, 3.0))
        self.assertEqual(router.command_endpoint, ("192.168.1.66", 5007))

    def test_vofa_command_returns_to_discovered_maix(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)
        router.route(b"10,20,30\n", ("192.168.1.66", 5007))

        kind, payload, target, values = router.route(
            b"track\n",
            ("127.0.0.1", 5006),
        )

        self.assertEqual(kind, "command")
        self.assertEqual(payload, b"track\n")
        self.assertEqual(target, ("192.168.1.66", 5007))
        self.assertIsNone(values)

    def test_command_waits_until_telemetry_is_discovered(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)

        kind, payload, target, _values = router.route(
            b"track\n",
            ("127.0.0.1", 5006),
        )

        self.assertEqual(kind, "command_unavailable")
        self.assertEqual(payload, b"track\n")
        self.assertIsNone(target)

    def test_invalid_telemetry_does_not_change_discovery(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)
        router.route(b"10,20,30\n", ("192.168.1.66", 5007))

        kind, _payload, _target, _values = router.route(
            b"1,nan,3\n",
            ("192.168.1.99", 5007),
        )

        self.assertEqual(kind, "invalid_telemetry")
        self.assertEqual(router.command_endpoint, ("192.168.1.66", 5007))

    def test_invalid_vofa_command_is_not_routed(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)
        router.route(b"10,20,30\n", ("192.168.1.66", 5007))

        kind, payload, target, _values = router.route(
            b"bad\x00command",
            ("127.0.0.1", 5006),
        )

        self.assertEqual(kind, "invalid_command")
        self.assertIsNone(payload)
        self.assertIsNone(target)
        self.assertEqual(router.command_endpoint, ("192.168.1.66", 5007))

    def test_numeric_vofa_feedback_cannot_become_motion_command(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)
        router.route(b"10,20,30\n", ("192.168.1.66", 5007))

        kind, payload, target, _values = router.route(
            b"80000\n",
            ("127.0.0.1", 5006),
        )

        self.assertEqual(kind, "invalid_command")
        self.assertIsNone(payload)
        self.assertIsNone(target)

    def test_explicit_command_host_overrides_discovery(self):
        router = UdpBridgeRouter(
            "127.0.0.1",
            5006,
            command_host="10.0.0.25",
            command_port=6007,
        )
        router.route(b"10,20,30\n", ("192.168.1.66", 5007))

        kind, payload, target, _values = router.route(
            b"stand\n",
            ("127.0.0.1", 5006),
        )

        self.assertEqual(kind, "command")
        self.assertEqual(payload, b"stand\n")
        self.assertEqual(target, ("10.0.0.25", 6007))
        self.assertIsNone(router.discovered_command_host)

    def test_command_host_override_is_optional(self):
        args = build_parser().parse_args([])
        self.assertIsNone(args.command_host)
        self.assertEqual(args.status_port, 5008)

    def test_valid_vision_status_is_parsed_and_discovers_maix(self):
        router = UdpBridgeRouter("127.0.0.1", 5006)
        payload = (
            b"VISION,5,LOCK_RETURN,2,1,HOMOGRAPHY,-12,7,10,0.875,15,123456\n"
        )

        kind, status = router.route_vision_status(
            payload, ("192.168.1.88", 51002)
        )

        self.assertEqual(kind, "vision")
        self.assertEqual(status.task, 5)
        self.assertEqual(status.phase, "LOCK_RETURN")
        self.assertEqual(status.shot, 2)
        self.assertEqual(status.locked, 1)
        self.assertEqual(status.geometry, "HOMOGRAPHY")
        self.assertEqual((status.dx, status.dy), (-12, 7))
        self.assertEqual(status.score, 10)
        self.assertAlmostEqual(status.quality, 0.875)
        self.assertEqual(status.milestones, 15)
        self.assertEqual(status.timestamp_ms, 123456)
        self.assertEqual(router.command_endpoint, ("192.168.1.88", 5007))

    def test_vision_status_formats_unknown_score(self):
        status = parse_vision_status(
            b"VISION,3,WAIT_MOVE,1,0,ELLIPSE,0,0,-1,0.5,2,9\n"
        )

        line = format_vision_status(status)

        self.assertIn("task=3", line)
        self.assertIn("LOST", line)
        self.assertIn("score=unknown", line)
        self.assertIn("milestones=0x2", line)

    def test_invalid_vision_status_is_rejected_without_discovery(self):
        invalid_payloads = (
            b"VISION,6,AIM,0,1,HOMOGRAPHY,0,0,10,1.0,0,1\n",
            b"VISION,2,AIM,3,1,HOMOGRAPHY,0,0,10,1.0,0,1\n",
            b"VISION,2,AIM,0,2,HOMOGRAPHY,0,0,10,1.0,0,1\n",
            b"VISION,2,AIM,0,1,HOMOGRAPHY,0,0,11,1.0,0,1\n",
            b"VISION,2,AIM,0,1,HOMOGRAPHY,0,0,10,nan,0,1\n",
            b"VISION,2,AIM,0,1,HOMOGRAPHY,0,0,10,1.1,0,1\n",
            b"VISION,2,AIM,0,1,HOMOGRAPHY,0,0,10,1.0,16,1\n",
            b"VISION,2,BAD PHASE,0,1,HOMOGRAPHY,0,0,10,1.0,0,1\n",
            b"NOTVISION,2,AIM,0,1,HOMOGRAPHY,0,0,10,1.0,0,1\n",
        )
        router = UdpBridgeRouter("127.0.0.1", 5006)

        for payload in invalid_payloads:
            with self.subTest(payload=payload):
                kind, status = router.route_vision_status(
                    payload, ("192.168.1.99", 5008)
                )
                self.assertEqual(kind, "invalid_vision")
                self.assertIsNone(status)
        self.assertIsNone(router.command_endpoint)


class SendUdpCommandTest(unittest.TestCase):
    def test_task_commands_are_preserved(self):
        for task in range(1, 6):
            command = "task%d" % task
            self.assertEqual(
                normalize_command(command), (command + "\n").encode("ascii")
            )
        self.assertEqual(normalize_command("stop"), b"stop\n")

    def test_chinese_task_aliases(self):
        expected = {
            "任务1": b"task1\n",
            "任务二": b"task2\n",
            "任务3": b"task3\n",
            "任务四": b"task4\n",
            "任务五": b"task5\n",
            "停止": b"stop\n",
        }
        for alias, payload in expected.items():
            with self.subTest(alias=alias):
                self.assertEqual(normalize_command(alias), payload)

    def test_unknown_unicode_command_is_rejected(self):
        with self.assertRaises(ValueError):
            normalize_command("未知命令")

    def test_bare_numeric_feedback_is_rejected(self):
        for value in ("0", "5", "7", "+350", "-350", "80000"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                normalize_command(value)


class UdpBridgeIntegrationTest(unittest.TestCase):
    @staticmethod
    def _bound_socket():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        sock.settimeout(2.0)
        return sock

    def test_5005_to_5006_5008_receive_and_5007_command(self):
        vofa = self._bound_socket()
        maix_command = self._bound_socket()
        listen_reservation = self._bound_socket()
        status_reservation = self._bound_socket()
        listen_port = listen_reservation.getsockname()[1]
        status_port = status_reservation.getsockname()[1]
        vofa_port = vofa.getsockname()[1]
        command_port = maix_command.getsockname()[1]
        listen_reservation.close()
        status_reservation.close()

        ready = threading.Event()
        errors = []

        def bridge_target():
            try:
                run_bridge(
                    "127.0.0.1",
                    listen_port,
                    "127.0.0.1",
                    vofa_port,
                    quiet=True,
                    max_packets=3,
                    command_port=command_port,
                    status_port=status_port,
                    ready_event=ready,
                )
            except BaseException as error:
                errors.append(error)
                ready.set()

        bridge = threading.Thread(target=bridge_target, daemon=True)
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            bridge.start()
            self.assertTrue(ready.wait(2.0), "UDP bridge did not bind in time")
            self.assertEqual(errors, [])

            sender.sendto(b"1.5,-2,3\n", ("127.0.0.1", listen_port))
            forwarded, _address = vofa.recvfrom(256)
            self.assertEqual(forwarded, b"1.5,-2,3\n")

            sender.sendto(
                b"VISION,5,LOCK_RETURN,2,1,HOMOGRAPHY,-12,7,10,0.875,15,123456\n",
                ("127.0.0.1", status_port),
            )
            vofa.sendto(b"task5\n", ("127.0.0.1", listen_port))
            command, _address = maix_command.recvfrom(64)
            self.assertEqual(command, b"task5\n")

            bridge.join(2.0)
            self.assertFalse(bridge.is_alive(), "UDP bridge did not process 5008")
            self.assertEqual(errors, [])
        finally:
            sender.close()
            vofa.close()
            maix_command.close()


if __name__ == "__main__":
    unittest.main()
