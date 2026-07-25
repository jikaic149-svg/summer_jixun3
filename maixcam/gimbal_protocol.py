"""Bounded JX1 line protocol shared by MaixCam and STM32."""

PROTOCOL_PREFIX = "JX1"
MAX_FRAME_BYTES = 384
MAX_PENDING_BYTES = 1024


class ProtocolError(ValueError):
    pass


class Frame:
    def __init__(self, kind, sequence, fields):
        self.kind = str(kind)
        self.sequence = int(sequence) & 0xFFFF
        self.fields = tuple(fields)

    def values(self):
        result = {}
        for field in self.fields:
            if "=" not in field:
                raise ProtocolError("field has no '=': %s" % field)
            key, value = field.split("=", 1)
            if not key:
                raise ProtocolError("field key is empty")
            result[key] = value
        return result


def crc16_ccitt(data):
    crc = 0xFFFF
    for value in data:
        crc ^= int(value) << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _validate_token(token):
    text = str(token)
    if not text or any(character in text for character in "|\r\n"):
        raise ProtocolError("invalid token")
    return text


def encode_frame(kind, sequence, fields=()):
    field_tokens = [_validate_token(field) for field in fields]
    payload = "|".join(field_tokens)
    header = "%s|%s|%d|%d" % (
        PROTOCOL_PREFIX, _validate_token(kind),
        int(sequence) & 0xFFFF, len(payload.encode("ascii")),
    )
    body = (header + ("|" + payload if payload else "")).encode("ascii")
    encoded = body + ("|%04X\n" % crc16_ccitt(body)).encode("ascii")
    if len(encoded) > MAX_FRAME_BYTES:
        raise ProtocolError("frame too large")
    return encoded


def decode_frame(line):
    if isinstance(line, str):
        raw = line.encode("ascii")
    else:
        raw = bytes(line)
    raw = raw.rstrip(b"\r\n")
    if not raw or len(raw) > MAX_FRAME_BYTES:
        raise ProtocolError("invalid frame length")
    body, separator, checksum = raw.rpartition(b"|")
    if not separator or len(checksum) != 4:
        raise ProtocolError("checksum missing")
    try:
        expected = int(checksum, 16)
    except ValueError as error:
        raise ProtocolError("checksum is not hexadecimal") from error
    if crc16_ccitt(body) != expected:
        raise ProtocolError("checksum mismatch")
    try:
        tokens = body.decode("ascii").split("|")
    except UnicodeDecodeError as error:
        raise ProtocolError("frame is not ASCII") from error
    if len(tokens) < 4 or tokens[0] != PROTOCOL_PREFIX:
        raise ProtocolError("protocol prefix mismatch")
    try:
        sequence = int(tokens[2], 10)
    except ValueError as error:
        raise ProtocolError("invalid sequence") from error
    if not 0 <= sequence <= 0xFFFF:
        raise ProtocolError("sequence out of range")
    try:
        declared_length = int(tokens[3], 10)
    except ValueError as error:
        raise ProtocolError("invalid payload length") from error
    fields = tokens[4:]
    actual_length = len("|".join(fields).encode("ascii"))
    if declared_length != actual_length:
        raise ProtocolError("payload length mismatch")
    return Frame(tokens[1], sequence, fields)


class StreamDecoder:
    def __init__(self):
        self.buffer = bytearray()
        self.error_count = 0

    def feed(self, data):
        frames = []
        for value in bytes(data):
            if value == 0x0A:
                if self.buffer:
                    try:
                        frames.append(decode_frame(bytes(self.buffer)))
                    except ProtocolError:
                        self.error_count += 1
                self.buffer.clear()
            elif value != 0x0D:
                if len(self.buffer) >= MAX_FRAME_BYTES:
                    self.buffer.clear()
                    self.error_count += 1
                else:
                    self.buffer.append(value)
        return frames
