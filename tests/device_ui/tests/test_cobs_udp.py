from __future__ import annotations

import socket
import time
from collections import Counter
from urllib.parse import urlparse

import pytest

from pages.api_client import DeviceApi
from test_cobs_ble import MESSAGE_TYPES, decode_cobs


GATAS_CONNECT_PORT = 3000
OBSERVATION_SECONDS = 15


def test_gatas_connect_udp_cobs(api: DeviceApi, device_url: str) -> None:
    modules = api.get("/api/Config/modules.json")
    assert isinstance(modules, str)
    missing = {"GatasConnect", "GatasConnectUDP"} - {name.strip() for name in modules.split(",")}
    if missing:
        pytest.skip(f"UDP COBS test not performed: enable {', '.join(sorted(missing))} and restart the device")

    host = urlparse(device_url).hostname
    assert host is not None
    device_ip = socket.gethostbyname(host)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((device_ip, GATAS_CONNECT_PORT))
        local_ip = route.getsockname()[0]

    config_path = "/api/Config/GatasConnectUDP.json"
    original = api.get(config_path)
    assert isinstance(original, dict) and isinstance(original.get("gatasServer"), dict)
    updated = {**original, "gatasServer": {**original["gatasServer"], "ip": local_ip}}
    counts: Counter[int] = Counter()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind((local_ip, GATAS_CONNECT_PORT))
        listener.settimeout(0.5)
        try:
            api.post(config_path, updated)
            deadline = time.monotonic() + OBSERVATION_SECONDS
            while time.monotonic() < deadline:
                try:
                    packet, sender = listener.recvfrom(65535)
                except socket.timeout:
                    continue
                if sender[0] != device_ip:
                    continue
                delimiter = packet.find(b"\0")
                assert delimiter > 0, f"missing COBS frame or delimiter: {packet.hex()}"
                # GatasConnectUDP sends one frame per datagram and pads the
                # remainder with zeros for the Android hotspot workaround.
                assert not any(packet[delimiter + 1 :]), "unexpected data after COBS frame"
                try:
                    payload = decode_cobs(packet[:delimiter])
                except ValueError as error:
                    pytest.fail(f"invalid COBS frame: {error}")
                assert payload, "COBS frame decoded to an empty payload"
                counts[payload[0]] += 1
        finally:
            api.post(config_path, original)

    print(f"\nObserved GatasConnect UDP messages on {local_ip}:{GATAS_CONNECT_PORT}:")
    for message_type, count in sorted(counts.items()):
        print(f"  {message_type}: {MESSAGE_TYPES.get(message_type, 'Unknown')} ({count})")
    assert counts[5], "no configuration broadcast received; check UDP output, routing and firewall"
