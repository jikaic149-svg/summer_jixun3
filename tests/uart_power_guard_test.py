import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class UartPowerGuardTest(unittest.TestCase):
    def test_usart6_tx_is_high_z_outside_dma_frames(self):
        source = (ROOT / "BSP" / "bsp_uart6.c").read_text(encoding="utf-8")
        self.assertIn("uart6_tx_pin_high_z", source)
        self.assertIn("gpio.Mode = GPIO_MODE_ANALOG", source)
        self.assertIn("uart6_tx_pin_active();", source)
        self.assertGreaterEqual(source.count("uart6_tx_pin_high_z();"), 5)

    def test_dma_start_is_atomic_and_startup_can_abort_tx(self):
        source = (ROOT / "BSP" / "bsp_uart6.c").read_text(encoding="utf-8")
        send_body = source.split("bool BSP_UART6_SendBytes", 1)[1].split(
            "void BSP_UART6_MuteTxForMs", 1
        )[0]
        self.assertIn("__disable_irq();", send_body)
        self.assertIn("tx_busy = 1U;", send_body)
        self.assertLess(send_body.index("tx_busy = 1U;"), send_body.index("memcpy"))
        self.assertIn("HAL_UART_AbortTransmit", source)

    def test_motor_start_mutes_reverse_uart_and_vofa_is_low_rate(self):
        gimbal = (ROOT / "Application" / "gimbal" / "gimbal_task.c").read_text(
            encoding="utf-8"
        )
        config = (ROOT / "Inc" / "config.h").read_text(encoding="utf-8")
        self.assertGreaterEqual(gimbal.count("BSP_UART6_MuteTxForMs"), 3)
        guard = re.search(r"VISION_UART_POWER_GUARD_MS\s+(\d+)U", config)
        period = re.search(r"VOFA_OUTPUT_PERIOD_MS\s+(\d+)U", config)
        self.assertIsNotNone(guard)
        self.assertIsNotNone(period)
        self.assertGreaterEqual(int(guard.group(1)), 500)
        self.assertGreaterEqual(int(period.group(1)), 100)


if __name__ == "__main__":
    unittest.main()
