from __future__ import annotations

import asyncio
from collections import Counter

import pytest


BINARY_CHARACTERISTIC = "0000ffe2-0000-1000-8000-00805f9b34fb"
OBSERVATION_SECONDS = 15
MAX_FRAME_SIZE = 256
MESSAGE_TYPES = {
    1: "Aircraft position V1", 2: "Ownship position request V1",
    4: "Select aircraft", 5: "Aircraft configuration V2", 6: "GDL90",
    7: "Set WiFi mode", 8: "Aircraft position V2", 9: "Aircraft position V3",
    10: "Ownship position request V2", 11: "Ownship pressure", 12: "Device action",
}


def decode_cobs(frame: bytes) -> bytes:
    decoded = bytearray()
    offset = 0
    while offset < len(frame):
        code = frame[offset]
        if code == 0 or offset + code > len(frame):
            raise ValueError("invalid COBS code")
        offset += 1
        end = offset + code - 1
        decoded.extend(frame[offset:end])
        offset = end
        if code != 0xFF and offset < len(frame):
            decoded.append(0)
    return bytes(decoded)


def test_cobs_ble_framing_and_message_overview(pytestconfig: pytest.Config) -> None:
    address = pytestconfig.getoption("--cobs-ble-address")
    if not address:
        pytest.skip("provide --cobs-ble-address or OPENACE_BLE_ADDRESS")
    bleak = pytest.importorskip("bleak", reason="install the optional ble dependencies")

    async def observe() -> tuple[bytes, Counter[int]]:
        stream = bytearray()
        async with bleak.BleakClient(address) as client:
            await client.start_notify(BINARY_CHARACTERISTIC, lambda _sender, data: stream.extend(data))
            await asyncio.sleep(OBSERVATION_SECONDS)
            await client.stop_notify(BINARY_CHARACTERISTIC)

        # The first notification may begin partway through a frame. Check only
        # complete frames after its first delimiter; retain adjacent zeroes.
        first = stream.find(b"\0")
        assert first >= 0, "no complete COBS frame received over BLE"
        complete = bytes(stream[first + 1 :])
        last = complete.rfind(b"\0")
        assert last >= 0, "fewer than two COBS frame delimiters received over BLE"
        frames = complete[:last].split(b"\0")
        counts: Counter[int] = Counter()
        for frame in frames:
            assert frame, "adjacent COBS delimiters (empty frame) in BLE stream"
            assert len(frame) + 1 <= MAX_FRAME_SIZE, "oversized COBS frame"
            try:
                payload = decode_cobs(frame)
            except ValueError as error:
                pytest.fail(f"invalid COBS frame {frame.hex()}: {error}")
            assert payload, "COBS frame decoded to an empty payload"
            counts[payload[0]] += 1
        return bytes(stream), counts

    stream, counts = asyncio.run(observe())
    print("\nObserved BLE COBS messages:")
    for message_type, count in sorted(counts.items()):
        print(f"  {message_type}: {MESSAGE_TYPES.get(message_type, 'Unknown')} ({count})")
    assert counts[5], f"no aircraft configuration broadcast observed in {len(stream)} bytes"
