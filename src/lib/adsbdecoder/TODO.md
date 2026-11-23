### Todo list

1. Handle vehicles on the ground
2. Send aircraft messages
3. Get gnn Altitude for if (mm->metype == 20 || mm->metype == 21 || mm->metype == 22)
4. Validate vertical climb rate
5. Multi threaded calculations of lat/lon?
6. Remove any aircraft more than XX above me to avoid excessive parsing
7. Keep cache of planes and it's altitude to avoid parsing
8. libmodes has thios brute force decoding, does dump1090 has that?
9. Preserve ADS-B pressure altitude from DF17 airborne position messages (type codes 9–18). The LR2021 and dump1090 paths share a decoder that uses the barometric value to estimate ellipsoid height and leaves `AircraftPositionInfo::pressureAlt` unavailable. Carry valid pressure altitude separately in metres through to the published traffic report.
