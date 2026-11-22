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


## Client ASI module verification

The project contains a 32-bit Windows client target named `iRxVoice2Helper.asi` and a native open.mp component named `drayvenguard-omp.dll` (Linux component builds produce `.so`). The ASI enumerates only loaded modules in the GTA process and only evaluates `.asi` modules. Its own module is excluded; the only permitted other ASI basename is `iRxvoice.asi`. Reports use custom RakNet RPC 221 on the existing game connection; no HTTP endpoint, extra UDP socket, or chat command is used.

The component challenges reports with a server-generated nonce, validates the response and kicks clients reporting another ASI with `Lotfan File Haye Ghir Mojhaz Inject Ya Load Nakonid`. Clients that do not complete a scan within 20 seconds are kicked with a missing-client/unsupported-version message.

The ASI accepts PE entry-point profiles SA-MP 0.3.7 R1 (`0x31DF13`) and SA-MP 0.3.DL R1 (`0x0FDB60`). An open.mp Client is accepted when its embedded `samp.dll` matches one of those two profiles and the server identifies it as open.mp. Other client builds display an unsupported-version message and do not install network hooks. Exact open.mp Client distribution compatibility still requires runtime smoke testing.

Build the ASI with a 32-bit Windows toolchain: `cmake -S . -B build-asi -A Win32 -DDG_BUILD_SERVER_PLUGIN=OFF -DDG_BUILD_CLIENT_ASI=ON`, then `cmake --build build-asi --config Release --target iRxVoice2Helper`.

Build the native component with: `cmake -S . -B build-omp -DDG_BUILD_SERVER_PLUGIN=OFF -DDG_BUILD_OMP_COMPONENT=ON`, then `cmake --build build-omp --config Release --target drayvenguard_omp_component`.

This is client telemetry, not tamper-proof attestation: a modified client can forge or suppress reports, and a different ASI renamed to the allowed basename can evade a basename-only allowlist. The scan does not inspect files elsewhere on the computer.
