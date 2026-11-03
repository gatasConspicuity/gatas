from __future__ import annotations

import time

import pytest

from pages.api_client import DeviceApi


def test_gatas_connect_provides_adsb_or_mlat_traffic_to_aircraft_tracker(api: DeviceApi) -> None:
    configured_modules = api.get("/api/Config/modules.json")
    assert isinstance(configured_modules, str)

    enabled_modules = {name.strip() for name in configured_modules.split(",")}
    if "GatasConnect" not in enabled_modules:
        pytest.skip("GatasConnect is not enabled on this device")

    deadline = time.monotonic() + 15
    last_tracker_data: dict | None = None
    while time.monotonic() < deadline:
        tracker_data = api.get("/api/AircraftTracker.json")
        assert isinstance(tracker_data, dict)
        last_tracker_data = tracker_data

        aircraft = tracker_data.get("aircraft:aoa")
        assert isinstance(aircraft, list), f"invalid AircraftTracker aircraft data: {tracker_data!r}"
        for row in aircraft:
            assert isinstance(row, str), f"invalid AircraftTracker aircraft row: {row!r}"
            fields = row.split(",")
            assert len(fields) == 4, f"invalid AircraftTracker aircraft row: {row!r}"
            if fields[1] in {"ADSB", "MLAT"}:
                return

        time.sleep(0.5)

    pytest.fail(
        "GatasConnect is enabled, but AircraftTracker reported no ADSB or MLAT aircraft "
        f"within 15 seconds; last response: {last_tracker_data!r}"
    )
