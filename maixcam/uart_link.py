"""MaixCAM2 到下位机的视觉/TOF UART 发送接口。

协议编码部分不依赖 ``maix``，可在 PC 上直接测试；只有 ``start()`` 才导入
MaixPy 的 ``pinmap`` 与 ``uart``。当前链路只发送，不解析下位机命令。
"""


FRAME_HEADER = b"\xAA\x55"
PROTOCOL_VERSION = 1
MESSAGE_VISION_TOF = 1
VISION_TOF_PAYLOAD_LENGTH = 9
VISION_TOF_FRAME_LENGTH = 16

FLAG_X_VALID = 0x01
FLAG_X_FINAL = 0x02
FLAG_DISTANCE_VALID = 0x04

INVALID_X = -1
INVALID_DISTANCE_MM = 0xFFFF


def crc16_modbus(data):
    """计算 Modbus CRC16，初值0xFFFF、多项式0xA001。"""
    crc = 0xFFFF
    for value in bytes(data):
        crc ^= int(value)
        for _bit in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def _append_u16_le(buffer, value):
    value = int(value) & 0xFFFF
    buffer.append(value & 0xFF)
    buffer.append((value >> 8) & 0xFF)


def _read_u16_le(data, offset):
    return int(data[offset]) | (int(data[offset + 1]) << 8)


def _clamp_int(value, minimum, maximum):
    return max(int(minimum), min(int(maximum), int(value)))


def encode_vision_tof_frame(packet, sequence=0):
    """把 ``_build_vision_tof_packet`` 的字典编码为固定16字节帧。

    ``x`` 使用有符号16位像素坐标；无有效水平中轴时为-1。距离使用无符号
    16位毫米值；无效时为0xFFFF。接收端必须优先检查flags，不能只检查数值。
    """
    x_valid = bool(
        packet.get("center_valid") and packet.get("horizontal_valid"))
    x_final = bool(x_valid and packet.get("center_final"))
    distance_valid = bool(packet.get("distance_valid"))

    if x_valid:
        x_value = _clamp_int(round(float(packet.get("cx", INVALID_X))),
                             -32768, 32767)
    else:
        x_value = INVALID_X
    if distance_valid:
        distance_mm = _clamp_int(
            round(float(packet.get("distance_mm", 0))),
            0, INVALID_DISTANCE_MM - 1)
    else:
        distance_mm = INVALID_DISTANCE_MM

    flags = 0
    if x_valid:
        flags |= FLAG_X_VALID
    if x_final:
        flags |= FLAG_X_FINAL
    if distance_valid:
        flags |= FLAG_DISTANCE_VALID

    body = bytearray((
        PROTOCOL_VERSION,
        MESSAGE_VISION_TOF,
        VISION_TOF_PAYLOAD_LENGTH,
    ))
    _append_u16_le(body, sequence)
    _append_u16_le(body, int(packet.get("frame", 0)))
    _append_u16_le(body, x_value)
    _append_u16_le(body, distance_mm)
    body.append(flags)

    crc = crc16_modbus(body)
    frame = bytearray(FRAME_HEADER)
    frame.extend(body)
    _append_u16_le(frame, crc)
    return bytes(frame)


def decode_vision_tof_frame(frame):
    """校验并解码一帧，供PC测试和下位机协议联调参考。"""
    data = bytes(frame)
    if len(data) != VISION_TOF_FRAME_LENGTH:
        raise ValueError("VISION_TOF frame length must be 16")
    if data[:2] != FRAME_HEADER:
        raise ValueError("invalid UART frame header")
    if data[2] != PROTOCOL_VERSION:
        raise ValueError("unsupported UART protocol version")
    if data[3] != MESSAGE_VISION_TOF:
        raise ValueError("unsupported UART message type")
    if data[4] != VISION_TOF_PAYLOAD_LENGTH:
        raise ValueError("invalid UART payload length")

    expected_crc = _read_u16_le(data, 14)
    actual_crc = crc16_modbus(data[2:14])
    if actual_crc != expected_crc:
        raise ValueError("UART CRC mismatch")

    x_raw = _read_u16_le(data, 9)
    x_value = x_raw - 0x10000 if x_raw & 0x8000 else x_raw
    flags = int(data[13])
    return {
        "sequence": _read_u16_le(data, 5),
        "frame": _read_u16_le(data, 7),
        "x": x_value,
        "distance_mm": _read_u16_le(data, 11),
        "flags": flags,
        "x_valid": bool(flags & FLAG_X_VALID),
        "x_final": bool(flags & FLAG_X_FINAL),
        "distance_valid": bool(flags & FLAG_DISTANCE_VALID),
        "crc": expected_crc,
    }


class VisionTofUartLink:
    """可选的MaixCAM2 UART2发送端；默认由配置保持关闭。"""

    def __init__(self, enabled=False, required=False,
                 device="/dev/ttyS2", baud=115200,
                 tx_pin="B0", rx_pin="B1",
                 tx_function="UART2_TX", rx_function="UART2_RX"):
        self.enabled = bool(enabled)
        self.required = bool(required)
        self.device = str(device)
        self.baud = int(baud)
        self.tx_pin = str(tx_pin)
        self.rx_pin = str(rx_pin)
        self.tx_function = str(tx_function)
        self.rx_function = str(rx_function)
        self.port = None
        self.available = False
        self.sequence = 0
        self.tx_count = 0
        self.error_count = 0
        self.last_error = ""
        self.last_frame = b""

    def start(self):
        if not self.enabled:
            return False
        try:
            from maix import pinmap, uart

            for pin, function in (
                    (self.tx_pin, self.tx_function),
                    (self.rx_pin, self.rx_function)):
                result = pinmap.set_pin_function(pin, function)
                if result not in (None, 0):
                    raise RuntimeError(
                        "pin mapping failed: %s -> %s, code=%s" %
                        (pin, function, str(result)))
            self.port = uart.UART(self.device, self.baud)
            self.available = True
            self.last_error = ""
            print("UART_OPEN_OK|device=%s|baud=%d|tx=%s|rx=%s" % (
                self.device, self.baud, self.tx_pin, self.rx_pin))
            return True
        except Exception as error:
            self.port = None
            self.available = False
            self.error_count += 1
            self.last_error = str(error)
            print("UART_OPEN_FAILED|%s" % self.last_error)
            if self.required:
                raise RuntimeError("required UART open failed: %s" %
                                   self.last_error)
            return False

    def send_packet(self, packet):
        """发送一份视觉/TOF字典；短写或异常时返回False，不终止视觉主循环。"""
        if not self.enabled or not self.available or self.port is None:
            return False
        frame = encode_vision_tof_frame(packet, self.sequence)
        self.sequence = (self.sequence + 1) & 0xFFFF
        self.last_frame = frame
        try:
            written = self.port.write(frame)
            if written is not None and int(written) != len(frame):
                raise OSError("UART short write %s/%s" %
                              (str(written), len(frame)))
            self.tx_count += 1
            self.last_error = ""
            return True
        except Exception as error:
            self.error_count += 1
            self.last_error = str(error)
            if self.port is not None:
                try:
                    self.port.close()
                except Exception:
                    pass
            self.port = None
            self.available = False
            print("UART_WRITE_FAILED|%s" % self.last_error)
            return False

    def close(self):
        if self.port is not None:
            try:
                self.port.close()
            except Exception:
                pass
        self.port = None
        self.available = False
