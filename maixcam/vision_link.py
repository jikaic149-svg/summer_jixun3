"""MaixCAM2 vision/TOF transport over the existing ESP TCP link."""

import socket

from gimbal_protocol import MAX_PENDING_BYTES, StreamDecoder, encode_frame
import vision_config as config


LISTEN_ADDRESS = "0.0.0.0"
LISTEN_PORT = 9000

VISION_FLAG_X_VALID = 0x01
VISION_FLAG_X_FINAL = 0x02
VISION_FLAG_DISTANCE_VALID = 0x04
VISION_FLAG_AIM_STABLE = 0x08


class _AimStabilityFilter:
    """Require consecutive final-center samples before arming auto aim."""

    def __init__(self, tolerance_px, required_samples):
        self.tolerance_px = max(0.0, float(tolerance_px))
        self.required_samples = max(1, int(required_samples))
        self.consecutive_samples = 0

    def update(self, packet):
        sample = dict(packet)
        frame_width = max(1, int(sample.get("frame_width", 640)))
        centered = bool(
            sample.get("center_valid") and
            sample.get("center_final") and
            sample.get("horizontal_valid") and
            abs(float(sample.get("cx", -1.0)) - frame_width * 0.5) <=
            self.tolerance_px
        )
        if centered:
            self.consecutive_samples += 1
        else:
            self.consecutive_samples = 0
        sample["aim_stable"] = bool(
            self.consecutive_samples >= self.required_samples)
        return sample


class _TcpPeer:
    def __init__(self):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((LISTEN_ADDRESS, LISTEN_PORT))
        self.server.listen(1)
        self.server.setblocking(False)
        self.client = None
        self.decoder = StreamDecoder()
        self.tx = bytearray()
        self.next_tx = bytearray()

    @property
    def connected(self):
        return self.client is not None

    def queue_latest(self, payload):
        payload = bytes(payload)
        if (self.client is None or len(payload) > MAX_PENDING_BYTES or
                (self.tx and len(self.tx) + len(payload) >
                 MAX_PENDING_BYTES)):
            return False
        if self.tx:
            self.next_tx.clear()
            self.next_tx.extend(payload)
        else:
            self.tx.extend(payload)
        return True

    def _close_client(self):
        if self.client is not None:
            try:
                self.client.close()
            except OSError:
                pass
        self.client = None
        self.tx.clear()
        self.next_tx.clear()
        self.decoder = StreamDecoder()

    def poll(self):
        frames = []
        if self.client is None:
            try:
                self.client, address = self.server.accept()
                self.client.setblocking(False)
                print("TCP CONNECT", address, flush=True)
            except OSError:
                return frames

        try:
            payload = self.client.recv(1024)
            if payload:
                frames.extend(self.decoder.feed(payload))
            else:
                print("TCP CLOSED", flush=True)
                self._close_client()
                return frames
        except OSError:
            pass

        if self.client is not None and self.tx:
            try:
                sent = self.client.send(self.tx)
                if sent > 0:
                    del self.tx[:sent]
                    if not self.tx and self.next_tx:
                        self.tx.extend(self.next_tx)
                        self.next_tx.clear()
            except OSError:
                self._close_client()
        return frames

    def close(self):
        self._close_client()
        self.server.close()


def encode_vision_sample(packet, sequence):
    """Encode one atomic x/distance sample as a CRC-protected JX1 VIS frame."""
    x_valid = bool(packet.get("center_valid") and
                   packet.get("horizontal_valid"))
    x_final = bool(x_valid and packet.get("center_final"))
    distance_valid = bool(packet.get("distance_valid"))
    aim_stable = bool(x_final and packet.get("aim_stable"))

    flags = 0
    if x_valid:
        flags |= VISION_FLAG_X_VALID
    if x_final:
        flags |= VISION_FLAG_X_FINAL
    if distance_valid:
        flags |= VISION_FLAG_DISTANCE_VALID
    if aim_stable:
        flags |= VISION_FLAG_AIM_STABLE

    x_value = int(round(float(packet.get("cx", -1)))) if x_valid else -1
    distance_mm = (int(round(float(packet.get("distance_mm", 0xFFFF))))
                   if distance_valid else 0xFFFF)
    frame_width = int(packet.get("frame_width", 640))
    fields = (
        "x=%d" % x_value,
        "d=%d" % distance_mm,
        "f=%d" % flags,
        "w=%d" % frame_width,
    )
    return encode_frame("VIS", sequence, fields)


class VisionTcpBridge:
    """Single-client bounded bridge; VIS is telemetry, never a fire command."""

    def __init__(self):
        self.peer = _TcpPeer()
        self.aim_filter = _AimStabilityFilter(
            getattr(config, "AUTO_AIM_X_TOLERANCE_PX", 8),
            getattr(config, "AUTO_AIM_STABLE_SAMPLES", 3),
        )
        self.last_packet_frame = None
        self.sequence = 0
        self.rx_frames = 0
        self.tx_frames = 0
        self.tx_dropped = 0
        print("VISION_TCP_READY port=%d" % LISTEN_PORT, flush=True)

    def poll(self, packet=None):
        for frame in self.peer.poll():
            self.rx_frames += 1
            if frame.kind == "ACK":
                print("GIMBAL ACK seq=%d %s" %
                      (frame.sequence, frame.values()), flush=True)
            elif frame.kind == "TEL" and (self.rx_frames == 1 or
                                           self.rx_frames % 10 == 0):
                print("GIMBAL TEL seq=%d %s" %
                      (frame.sequence, frame.values()), flush=True)

        if packet is None:
            return
        packet_frame = packet.get("frame")
        if (packet_frame is not None and
                packet_frame == self.last_packet_frame):
            return
        self.last_packet_frame = packet_frame
        packet = self.aim_filter.update(packet)
        payload = encode_vision_sample(packet, self.sequence)
        self.sequence = (self.sequence + 1) & 0xFFFF
        if self.peer.queue_latest(payload):
            self.tx_frames += 1
        else:
            self.tx_dropped += 1
            if self.tx_dropped == 1 or self.tx_dropped % 20 == 0:
                print("VISION_TCP_DROP count=%d connected=%d" %
                      (self.tx_dropped, 1 if self.peer.connected else 0),
                      flush=True)

    def close(self):
        self.peer.close()
        print("VISION_TCP_CLOSED tx=%d drop=%d rx=%d" %
              (self.tx_frames, self.tx_dropped, self.rx_frames), flush=True)
