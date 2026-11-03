from __future__ import annotations

import os
import time
import uuid
from collections.abc import Iterator

import pytest

from pages.api_client import DeviceApi


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("openace-device")
    group.addoption(
        "--device-url",
        action="store",
        default=os.environ.get("OPENACE_DEVICE_URL"),
        help="OpenAce device base URL (or set OPENACE_DEVICE_URL)",
    )
    group.addoption(
        "--run-destructive",
        action="store_true",
        help="Run tests that erase stores or enter firmware-update mode",
    )
    group.addoption(
        "--run-radio-traffic",
        action="store_true",
        help="Run tests that require a second GATAS transmitting radio traffic",
    )
    group.addoption(
        "--cobs-ble-address",
        default=os.environ.get("OPENACE_BLE_ADDRESS"),
        help="BLE address or macOS device UUID for the COBS stream test",
    )


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    if not config.getoption("--run-destructive"):
        skip = pytest.mark.skip(reason="requires --run-destructive")
        for item in items:
            if "destructive" in item.keywords:
                item.add_marker(skip)

    if not config.getoption("--run-radio-traffic"):
        skip = pytest.mark.skip(reason="requires --run-radio-traffic and a second transmitting GATAS")
        for item in items:
            if "radio_traffic" in item.keywords:
                item.add_marker(skip)


@pytest.fixture(scope="session")
def device_url(pytestconfig: pytest.Config) -> str:
    value = pytestconfig.getoption("--device-url")
    if not value:
        pytest.skip("provide --device-url or OPENACE_DEVICE_URL")
    return str(value).rstrip("/")


@pytest.fixture(scope="session")
def api(device_url: str) -> DeviceApi:
    client = DeviceApi(device_url)
    client.wait_ready(timeout=15)
    return client


@pytest.fixture
def temporary_callsign() -> str:
    return f"T{uuid.uuid4().hex[:6]}".upper()


@pytest.fixture
def original_general_config(api: DeviceApi) -> dict:
    return api.get("/api/Config/config.json")


@pytest.fixture
def temporary_aircraft(
    api: DeviceApi,
    temporary_callsign: str,
    original_general_config: dict,
) -> Iterator[dict]:
    aircraft = {
        "callSign": temporary_callsign,
        "category": "Light",
        "addressType": "OGN",
        "address": 0x123456,
        "noTrack": False,
        "groundStation": False,
        "heightAboveGps": 0,
        "protocols": [],
    }
    assert api.get(f"/api/Config/aircraft/{temporary_callsign}.json") is None
    api.post(f"/api/Config/aircraft/{temporary_callsign}.json", aircraft)
    assert api.wait_for_aircraft(temporary_callsign, timeout=5) == aircraft

    yield aircraft

    # Move the active selection away from the temporary record before deleting
    # it. Deleting the selected record first can leave the device configuration
    # pointing at a missing aircraft and contaminate every later test.
    api.post("/api/Config/config.json", original_general_config)
    assert api.wait_for_active_aircraft(original_general_config["aircraftId"])
    api.delete_aircraft(temporary_callsign)
    if not original_general_config.get("_dirty", True):
        api.save()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if api.get(f"/api/Config/aircraft/{temporary_callsign}.json") is None:
            break
        time.sleep(0.1)
    else:
        pytest.fail(f"temporary aircraft {temporary_callsign} was not removed")
