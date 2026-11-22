# DrayvenGUARD

DrayvenGUARD is a native C++ server plugin with a Pawn filterscript integration layer for SA-MP-compatible servers, including open.mp.

It is designed as a server-side sanity-check layer. It does not inject into the GTA client, modify game memory, or claim to detect every cheat.

## Detection

- Invalid or non-finite player coordinates and out-of-world positions
- Abnormal on-foot movement between server-observed updates
- Extreme vehicle movement between updates, with a deliberately generous aircraft-friendly limit
- Health and armour values outside the supported 0-100 range
- Invalid weapon IDs and implausible ammunition values
- Violation scoring with cooldown and gradual decay to reduce one-packet false positives
- Per-player reset, enable/disable, and teleport grace APIs

The default kick threshold is six violation points. A violation point is added no more than once every 1.5 seconds while suspicious data continues; clean play gradually removes points. Tune and test this policy on your own server before enabling automatic kicks in production.

## Build

GitHub Actions builds x64 Windows and Linux shared libraries on every push to `main`. Download the matching artifact from the Actions run.

Local build requirements: CMake 3.20+, a C++17 compiler, and network access for the pinned SA-MP plugin SDK dependency.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
```

Output:
- Windows: `build/bin/drayvenguard.dll`
- Linux: `build/bin/drayvenguard.so`

## Install on open.mp

1. Copy the matching plugin binary to the server's `plugins` directory.
2. Add `"drayvenguard"` to `pawn.legacy_plugins` in `config.json`. open.mp resolves the platform-specific `.dll` or `.so`.
3. Copy `include/drayvenguard.inc` into your Pawn include directory (for example, `qawno/include`).
4. Compile `filterscripts/DrayvenGUARD.pwn` and place the resulting `DrayvenGUARD.amx` in the server's `filterscripts` directory.
5. Add `"filterscripts/DrayvenGUARD"` to `pawn.side_scripts` in `config.json`, then restart the server.

Example configuration:

```json
{
  "pawn": {
    "legacy_plugins": ["drayvenguard"],
    "side_scripts": ["filterscripts/DrayvenGUARD"]
  }
}
```

The filterscript runs independently of the gamemode, so an existing gamemode does not need to be edited just to enable the base checks.

## Teleports and scripted movement

The filterscript cannot infer whether a large position change was an authorized teleport. Before a legitimate teleport in your gamemode, call:

```pawn
DG_AllowTeleport(playerid, 5000);
SetPlayerPos(playerid, x, y, z);
```

For a respawn or custom spawn flow, call `DG_ResetPlayer(playerid)` after the server has placed the player. The included filterscript already resets the baseline on connect and spawn.

## Pawn API

```pawn
DG_CheckPlayer(playerid, Float:x, Float:y, Float:z, Float:health, Float:armour, weaponid, ammo, vehicleid, interiorid, worldid, specialaction);
DG_ResetPlayer(playerid);
DG_AllowTeleport(playerid, duration = 5000);
DG_GetViolationLevel(playerid);
DG_GetLastViolation(playerid);
DG_SetEnabled(playerid, enabled);
DG_ProcessPlayerUpdate(playerid);
DG_PrepareTeleport(playerid, duration = 5000);
```

Violation bit flags:
- `1`: invalid position
- `2`: abnormal movement
- `4`: invalid health
- `8`: invalid armour
- `16`: invalid weapon ID
- `32`: implausible ammunition

## Important limitations

This is a practical baseline, not a kernel anti-cheat or a client-integrity verifier. Client-side cheats that stay within plausible server-observed values, silent aim, wallhack, aimbot, modified game files, and sophisticated packet manipulation require additional server-side gameplay validation, telemetry, or a separately designed client component. Do not treat a single movement anomaly as proof of cheating.
