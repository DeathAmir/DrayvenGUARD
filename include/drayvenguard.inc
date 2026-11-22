#if defined _DRAYVENGUARD_INCLUDED
    #endinput
#endif
#define _DRAYVENGUARD_INCLUDED

native DG_CheckPlayer(playerid, Float:x, Float:y, Float:z, Float:health, Float:armour, weaponid, ammo, vehicleid, interiorid, worldid, specialaction);
native DG_ResetPlayer(playerid);
native DG_AllowTeleport(playerid, duration = 5000);
native DG_GetViolationLevel(playerid);
native DG_GetLastViolation(playerid);
native DG_SetEnabled(playerid, enabled);

#define DG_MAX_VIOLATIONS (6)
#define DG_VIOLATION_POSITION (1)
#define DG_VIOLATION_MOVEMENT (2)
#define DG_VIOLATION_HEALTH (4)
#define DG_VIOLATION_ARMOUR (8)
#define DG_VIOLATION_WEAPON (16)
#define DG_VIOLATION_AMMO (32)

stock DG_ProcessPlayerUpdate(playerid)
{
    new Float:x, Float:y, Float:z;
    new Float:health, Float:armour;

    GetPlayerPos(playerid, x, y, z);
    GetPlayerHealth(playerid, health);
    GetPlayerArmour(playerid, armour);

    DG_CheckPlayer(
        playerid,
        x, y, z,
        health, armour,
        GetPlayerWeapon(playerid),
        GetPlayerAmmo(playerid),
        GetPlayerVehicleID(playerid),
        GetPlayerInterior(playerid),
        GetPlayerVirtualWorld(playerid),
        GetPlayerSpecialAction(playerid)
    );

    if (DG_GetViolationLevel(playerid) >= DG_MAX_VIOLATIONS)
    {
        Kick(playerid);
        return 0;
    }

    return 1;
}

stock DG_PrepareTeleport(playerid, duration = 5000)
{
    return DG_AllowTeleport(playerid, duration);
}
