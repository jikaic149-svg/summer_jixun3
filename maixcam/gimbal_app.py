"""MaixCam touchscreen mirror, TCP endpoint and MaixVision telemetry console."""

import socket

from maix import app, display, image, network, time, touchscreen

from gimbal_protocol import MAX_PENDING_BYTES, StreamDecoder, encode_frame
from gimbal_ui import GimbalUi, LOGICAL_HEIGHT, LOGICAL_WIDTH, draw_ui
from vofa_protocol import CHANNEL_NAMES, VOFA_TCP_PORT, make_sample
from vofa_stream import VofaStreamServer


LISTEN_ADDRESS = "0.0.0.0"
LISTEN_PORT = 9000
FRAME_PERIOD_MS = 50

FIELD_NAMES = {
    "t": "ms", "s": "status", "f": "feedback", "d": "distance",
    "y": "yaw_mdeg", "p": "pitch_mdeg", "g": "target_mdeg",
    "c": "cmd_ma", "i": "fb_ma", "v": "speed_mrpm",
    "yr": "yaw_encoder_raw", "pr": "pitch_encoder_raw",
    "yv": "yaw_feedback_valid", "pv": "pitch_feedback_valid",
    "h": "pitch_target_mdeg", "pc": "pitch_cmd_ma",
    "pi": "pitch_fb_ma",
    "ys": "yaw_state", "ps": "pitch_state", "a": "scan",
    "e": "fault", "x": "fault_code", "r": "responses",
    "n": "commands", "lo": "min_mdeg", "hi": "max_mdeg",
    "rp": "pitch_responses",
    "pk": "peak_ma", "er": "esp_rx_drop", "et": "esp_tx",
    "ur": "uart_rx_drop", "pe": "proto_err",
}


def _expand_fields(values):
    return {FIELD_NAMES.get(key, key): value for key, value in values.items()}


class TcpPeer:
    def __init__(self):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((LISTEN_ADDRESS, LISTEN_PORT))
        self.server.listen(1)
        self.server.setblocking(False)
        self.client = None
        self.decoder = StreamDecoder()
        self.tx = bytearray()

    @property
    def connected(self):
        return self.client is not None

    def queue(self, payload):
        payload = bytes(payload)
        if self.client is None or len(self.tx) + len(payload) > MAX_PENDING_BYTES:
            return False
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
            except OSError:
                pass
        return frames

    def close(self):
        self._close_client()
        self.server.close()


def _touch_to_logical(ts, img, disp):
    screen_x, screen_y, pressed = ts.read()
    x, y = image.resize_map_pos_reverse(
        img.width(), img.height(), disp.width(), disp.height(),
        image.Fit.FIT_CONTAIN, screen_x, screen_y,
    )
    return max(0, min(LOGICAL_WIDTH - 1, int(x))), max(0, min(LOGICAL_HEIGHT - 1, int(y))), bool(pressed)


def main():
    font_paths = (
        "/maixapp/share/font/SourceHanSansCN-Regular.otf",
        "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
    )
    for font_path in font_paths:
        try:
            image.load_font("gimbal_cn", font_path, size=16)
            image.set_default_font("gimbal_cn")
            print("GIMBAL_FONT", font_path, flush=True)
            break
        except Exception:
            continue
    wifi = network.wifi.Wifi()
    disp = display.Display()
    ts = touchscreen.TouchScreen()
    canvas = image.Image(LOGICAL_WIDTH, LOGICAL_HEIGHT)
    ui = GimbalUi()
    peer = TcpPeer()
    vofa_peer = VofaStreamServer()
    latest_telemetry = {}
    latest_debug = {}
    command_sequence = 0
    next_frame_ms = 0
    print("GIMBAL_UI_READY ip=%s port=%d vofa=%d display=%dx%d" %
          (wifi.get_ip(), LISTEN_PORT, VOFA_TCP_PORT,
           disp.width(), disp.height()), flush=True)
    print("VOFA_CHANNELS " + ",".join(CHANNEL_NAMES), flush=True)

    try:
        while not app.need_exit():
            vofa_peer.poll()
            for frame in peer.poll():
                try:
                    values = _expand_fields(frame.values())
                except Exception as error:
                    print("PROTOCOL FIELD ERROR", error, flush=True)
                    continue
                if frame.kind == "TEL":
                    latest_telemetry.update(values)
                    ui.apply_telemetry(values)
                    vofa_peer.publish(make_sample(latest_telemetry,
                                                  latest_debug))
                    print("GIMBAL TEL seq=%d %s" % (frame.sequence, values), flush=True)
                elif frame.kind == "ACK":
                    ui.set_ack(values.get("command", "CMD"), values.get("result", "?"))
                    print("GIMBAL ACK seq=%d %s" % (frame.sequence, values), flush=True)
                elif frame.kind == "DBG":
                    latest_debug.update(values)
                    print("GIMBAL DBG seq=%d %s" % (frame.sequence, values), flush=True)
                else:
                    print("GIMBAL UNKNOWN kind=%s seq=%d" % (frame.kind, frame.sequence), flush=True)

            ui.link_online = peer.connected
            x, y, pressed = _touch_to_logical(ts, canvas, disp)
            action = ui.touch(x, y, pressed)
            if action is not None:
                command, value = action
                command_sequence = (command_sequence + 1) & 0xFFFF
                payload = encode_frame("CMD", command_sequence,
                                       ("name=%s" % command, "value=%d" % value))
                if peer.queue(payload):
                    ui.feedback = "%s SENT" % command
                    print("GIMBAL CMD seq=%d name=%s value=%d" %
                          (command_sequence, command, value), flush=True)
                else:
                    ui.feedback = "TCP NOT READY"

            now_ms = time.ticks_ms()
            if now_ms >= next_frame_ms:
                draw_ui(canvas, image, ui)
                disp.show(canvas, fit=image.Fit.FIT_CONTAIN)
                next_frame_ms = now_ms + FRAME_PERIOD_MS
            time.sleep_ms(5)
    finally:
        vofa_peer.close()
        peer.close()
        print("GIMBAL_UI_STOPPED", flush=True)


if __name__ == "__main__":
    main()
