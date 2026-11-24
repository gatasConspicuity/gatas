### Todo list

1. Handle vehicles on the ground
2. Send aircraft messages
3. Get gnn Altitude for if (mm->metype == 20 || mm->metype == 21 || mm->metype == 22)
4. Validate vertical climb rate
5. Multi threaded calculations of lat/lon?
6. Remove any aircraft more than XX above me to avoid excessive parsing
7. Keep cache of planes and it's altitude to avoid parsing
8. libmodes has thios brute force decoding, does dump1090 has that?
