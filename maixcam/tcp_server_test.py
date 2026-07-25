"""Minimal MaixCam TCP echo server for ESP8266 link bring-up."""

import socket

from maix import app, network


LISTEN_ADDRESS = "0.0.0.0"
LISTEN_PORT = 9000
GREETING = b"MAIX_TO_ESP\n"
REPLY = b"MAIX_ACK\n"
SOCKET_TIMEOUT_SECONDS = 0.2


def main() -> None:
    wifi = network.wifi.Wifi()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((LISTEN_ADDRESS, LISTEN_PORT))
    server.listen(1)
    server.settimeout(SOCKET_TIMEOUT_SECONDS)
    print(f"TCP_SERVER_READY ip={wifi.get_ip()} port={LISTEN_PORT}", flush=True)

    client = None
    try:
        while not app.need_exit():
            if client is None:
                try:
                    client, address = server.accept()
                    client.settimeout(SOCKET_TIMEOUT_SECONDS)
                    print(f"TCP_CLIENT_CONNECTED address={address}", flush=True)
                    client.sendall(GREETING)
                    print(f"TCP_TX bytes={len(GREETING)} data={GREETING!r}",
                          flush=True)
                except socket.timeout:
                    continue

            try:
                payload = client.recv(1024)
                if not payload:
                    client.close()
                    client = None
                    print("TCP_CLIENT_CLOSED", flush=True)
                    continue
                print(f"TCP_RX bytes={len(payload)} data={payload!r}", flush=True)
                client.sendall(REPLY)
                print(f"TCP_TX bytes={len(REPLY)} data={REPLY!r}", flush=True)
            except socket.timeout:
                continue
            except OSError as error:
                print(f"TCP_CLIENT_ERROR error={error}", flush=True)
                client.close()
                client = None
    finally:
        if client is not None:
            client.close()
        server.close()
        print("TCP_SERVER_STOPPED", flush=True)


if __name__ == "__main__":
    main()
