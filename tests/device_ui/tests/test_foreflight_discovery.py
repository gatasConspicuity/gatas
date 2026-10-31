from __future__ import annotations

import json
import socket
import time
from urllib.parse import urlparse

import pytest

from pages.api_client import DeviceApi


FOREFLIGHT_DISCOVERY_PORT = 63093
MINIMUM_GDL90_PACKETS = 3


def contains_gdl90_heartbeat(packet: bytes) -> bool:
    """Return whether a UDP datagram contains a framed GDL90 heartbeat."""
    for framed_message in packet.split(b"\x7e")[1:]:
        if not framed_message:
            continue

        unescaped = bytearray()
        iterator = iter(framed_message)
        for byte in iterator:
            if byte == 0x7D:
                try:
                    byte = next(iterator) ^ 0x20
                except StopIteration:
                    break
            unescaped.append(byte)

        if unescaped and unescaped[0] == 0x00:
            return True
    return False


def test_foreflight_advertisement_starts_gdl90_udp_stream(
    api: DeviceApi,
    device_url: str,
) -> None:
    configured_modules = api.get("/api/Config/modules.json")
    assert isinstance(configured_modules, str)
    enabled_modules = {name.strip() for name in configured_modules.split(",")}
    required_modules = {"GDLoverUDP", "Gdl90Service"}
    if not required_modules.issubset(enabled_modules):
        pytest.skip(f"device must enable {', '.join(sorted(required_modules))}")

    device_host = urlparse(device_url).hostname
    assert device_host is not None
    device_ip = socket.gethostbyname(device_host)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((device_ip, FOREFLIGHT_DISCOVERY_PORT))
        local_ip = route.getsockname()[0]

    diagnostics = api.get("/api/GDLoverUDP.json")
    assert isinstance(diagnostics, dict)
    clients = diagnostics.get("foreFlightClients", [])
    if any(client.get("ip") == local_ip for client in clients if isinstance(client, dict)):
        pytest.skip(f"ForeFlight discovery not tested: {local_ip} is already registered on the device")
    if diagnostics.get("currentPortsInUse", 0) >= diagnostics.get("maxPorts", 1):
        pytest.skip("ForeFlight discovery not tested: all GDLoverUDP port slots are already in use")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("", 0))
        listener.settimeout(2.0)
        gdl90_port = listener.getsockname()[1]

        with pytest.raises(socket.timeout):
            listener.recvfrom(65535)

        advertisement = json.dumps(
            {"App": "ForeFlight", "GDL90": {"port": gdl90_port}}
        ).encode()

        packets: list[bytes] = []
        deadline = time.monotonic() + 10
        next_advertisement = 0.0
        listener.settimeout(0.5)
        while len(packets) < MINIMUM_GDL90_PACKETS and time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_advertisement:
                listener.sendto(advertisement, (device_host, FOREFLIGHT_DISCOVERY_PORT))
                next_advertisement = now + 1.0
            try:
                packet, sender = listener.recvfrom(65535)
            except socket.timeout:
                continue
            if sender[0] == socket.gethostbyname(device_host):
                packets.append(packet)

    assert len(packets) >= MINIMUM_GDL90_PACKETS, (
        f"received only {len(packets)} UDP packets on advertised port {gdl90_port}"
    )
    assert any(contains_gdl90_heartbeat(packet) for packet in packets), (
        "received UDP traffic, but none of the packets contained a GDL90 heartbeat"
    )
