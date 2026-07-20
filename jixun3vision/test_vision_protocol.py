import unittest

from vision_target_link import (
    UdpVisionTargetLink,
    VisionTargetLink,
    build_status_payload,
    format_vision_frame,
    parse_task_status,
    parse_vision_frame,
)


class FakePort:
    def __init__(self):
        self.writes = []

    def write(self, payload):
        self.writes.append(payload)


class FakeUdpSocket:
    def __init__(self, packets=None):
        self.packets = list(packets or [])
        self.sent = []

    def recvfrom(self, _size):
        if not self.packets:
            raise BlockingIOError()
        return self.packets.pop(0)

    def sendto(self, payload, address):
        self.sent.append((payload, address))


class VisionProtocolTest(unittest.TestCase):
    def test_legacy_udp_sender_uses_current_wlan_by_default(self):
        link = UdpVisionTargetLink()

        self.assertEqual(link.host, "192.168.237.147")
        self.assertEqual(link.port, 5005)

    def test_old_and_new_vision_frames_parse(self):
        old = parse_vision_frame("-3,4,1\n")
        new = parse_vision_frame("V,-3,4,1,1,0,2\n")
        self.assertTrue(old["legacy"])
        self.assertEqual((old["dx"], old["dy"], old["valid"]), (-3, 4, True))
        self.assertFalse(new["legacy"])
        self.assertEqual(new["move_epoch"], 2)

    def test_task_status_parses_milestone_mask(self):
        status = parse_task_status("T,5,LOCK_RETURN,2,1,0,0x0f,1\n")
        self.assertEqual(status.task_id, 5)
        self.assertEqual(status.phase, "LOCK_RETURN")
        self.assertEqual(status.shot_index, 2)
        self.assertEqual(status.milestones, 15)
        self.assertTrue(status.done)

    def test_numeric_mcu_phase_is_mapped_for_ui_and_udp(self):
        status = parse_task_status("T,3,4,1,0,0,2,0\n")
        self.assertEqual(status.phase, "WAIT_MOVE")

    def test_new_uart_packet_has_all_safety_fields(self):
        link = VisionTargetLink(period_ms=0, yaw_only=False)
        link.port = FakePort()
        link.available = True
        self.assertTrue(link.send_vision(-5, 6, True, True, False, 3, 100))
        self.assertEqual(link.port.writes, [b"V,-5,6,1,1,0,3\n"])
        self.assertEqual(
            format_vision_frame(100, 200, False, True, True, 7),
            "V,0,0,0,0,0,7\n",
        )

    def test_status_payload_matches_pc_5008_contract(self):
        payload = build_status_payload(
            4, "WAIT_MOVE", 1, True, "quad", -2, 3, -1, 0.875, 3, 12345,
        )
        self.assertEqual(
            payload,
            b"VISION,4,WAIT_MOVE,1,1,quad,-2,3,-1,0.875,3,12345\n",
        )

    def test_command_whitelist_keeps_tasks_and_pid_but_rejects_arbitrary_text(self):
        self.assertEqual(VisionTargetLink._normalize_command(b"task5"), b"task5\n")
        self.assertEqual(VisionTargetLink._normalize_command(b"kp=1.25"), b"kp=1.25\n")
        self.assertEqual(VisionTargetLink._normalize_command(b"mode 5"), b"mode 5\n")
        self.assertEqual(VisionTargetLink._normalize_command(b"mode 7"), b"mode 7\n")
        self.assertEqual(VisionTargetLink._normalize_command(b"bias=-1.5"), b"bias=-1.5\n")
        self.assertIsNone(VisionTargetLink._normalize_command(b"120.0"))
        self.assertIsNone(VisionTargetLink._normalize_command(b"80000"))
        self.assertIsNone(VisionTargetLink._normalize_command(b"5"))
        self.assertIsNone(VisionTargetLink._normalize_command(b"+350"))
        self.assertIsNone(VisionTargetLink._normalize_command(b"-350"))
        self.assertIsNone(VisionTargetLink._normalize_command(b"7"))
        self.assertEqual(
            VisionTargetLink._normalize_command(b"cal_laser_on"),
            b"cal_laser_on\n",
        )
        self.assertEqual(
            VisionTargetLink._normalize_command(b"cal_laser_off"),
            b"cal_laser_off\n",
        )
        self.assertIsNone(VisionTargetLink._normalize_command(b"reboot now"))

    def test_clearing_status_prevents_previous_done_from_finishing_new_task(self):
        link = VisionTargetLink()
        link.task_status = parse_task_status("T,2,6,1,1,0,0,1\n")
        link.clear_task_status(4)
        self.assertFalse(link.task_status.done)
        self.assertFalse(link.task_status.valid)
        self.assertEqual(link.task_status.phase, "STARTING")

    def test_udp_status_uses_dedicated_5008_destination(self):
        link = VisionTargetLink(
            telemetry_host="192.168.237.147", status_port=5008
        )
        link.telemetry_sock = FakeUdpSocket()
        payload = b"VISION,1,IDLE,0,0,none,0,0,-1,0.000,0,1\n"
        self.assertTrue(link.send_status(payload))
        self.assertEqual(
            link.telemetry_sock.sent,
            [(payload, ("192.168.237.147", 5008))],
        )

    def test_udp_commands_check_source_and_whitelist_before_uart(self):
        link = VisionTargetLink(
            telemetry_host="192.168.237.147",
            command_host="192.168.237.147",
        )
        link.port = FakePort()
        link.command_sock = FakeUdpSocket([
            (b"task2", ("10.249.6.99", 6000)),
            (b"reboot", ("192.168.237.147", 6000)),
            (b"task2", ("192.168.237.147", 6000)),
        ])
        self.assertEqual(link._poll_commands(), 0)
        self.assertEqual(link._poll_commands(), 1)
        self.assertEqual(link.port.writes, [b"task2\n"])
        self.assertEqual(link.command_drop_count, 2)


if __name__ == "__main__":
    unittest.main()
