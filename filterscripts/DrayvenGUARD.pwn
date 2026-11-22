#include <a_samp>
#include <drayvenguard>

public OnFilterScriptInit()
{
    print("[DrayvenGUARD] Anti-cheat filterscript started.");
    return 1;
}

public OnPlayerConnect(playerid)
{
    DG_ResetPlayer(playerid);
    return 1;
}

public OnPlayerSpawn(playerid)
{
    DG_ResetPlayer(playerid);
    return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
    DG_ResetPlayer(playerid);
    return 1;
}

public OnPlayerUpdate(playerid)
{
    return DG_ProcessPlayerUpdate(playerid);
}
