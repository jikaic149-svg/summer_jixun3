"""Non-blocking single-observer TCP server for PC-side VOFA bridging."""

import socket

try:
    from vofa_protocol import VOFA_TCP_PORT, encode_firewater
except ImportError:
    from .vofa_protocol import VOFA_TCP_PORT, encode_firewater


MAX_PENDING_BYTES = 2048


class VofaStreamServer:
    def __init__(self, address="0.0.0.0", port=VOFA_TCP_PORT):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((address, port))
        self.server.listen(1)
        self.server.setblocking(False)
        self.client = None
        self.tx = bytearray()
        self.dropped = 0

    def _close_client(self):
        if self.client is not None:
            try:
                self.client.close()
            except OSError:
                pass
        self.client = None
        self.tx.clear()

    def publish(self, sample):
        if self.client is None:
            return False
        payload = encode_firewater(sample)
        if len(self.tx) + len(payload) > MAX_PENDING_BYTES:
            self.tx.clear()
            self.dropped += 1
        self.tx.extend(payload)
        return True

    def poll(self):
        if self.client is None:
            try:
                self.client, address = self.server.accept()
                self.client.setblocking(False)
                print("VOFA CONNECT", address, flush=True)
            except OSError:
                return

        try:
            received = self.client.recv(64)
            if not received:
                print("VOFA CLOSED", flush=True)
                self._close_client()
                return
        except OSError:
            pass

        if self.client is not None and self.tx:
            try:
                sent = self.client.send(self.tx)
                if sent > 0:
                    del self.tx[:sent]
            except OSError:
                self._close_client()

    def close(self):
        self._close_client()
        self.server.close()
