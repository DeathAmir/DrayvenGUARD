#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "amx/amx.h"
#include "plugincommon.h"

namespace
{
    constexpr int MaxPlayers = 1004;
    constexpr int MaxViolationLevel = 20;
    constexpr int KickViolationLevel = 6;
    constexpr std::uint64_t TeleportGraceMs = 5000;
    constexpr std::uint64_t ViolationCooldownMs = 1500;
    constexpr std::uint64_t CleanDecayMs = 5000;

    enum Violation : cell
    {
        ViolationNone = 0,
        ViolationPosition = 1,
        ViolationMovement = 2,
        ViolationHealth = 4,
        ViolationArmour = 8,
        ViolationWeapon = 16,
        ViolationAmmo = 32
    };

    struct PlayerState
    {
        bool initialized = false;
        bool enabled = true;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint64_t lastUpdate = 0;
        std::uint64_t graceUntil = 0;
        std::uint64_t lastViolationAt = 0;
        std::uint64_t lastDecayAt = 0;
        int vehicleId = 0;
        int interiorId = 0;
        int worldId = 0;
        int violationLevel = 0;
        cell lastViolation = ViolationNone;
    };

    std::array<PlayerState, MaxPlayers> players{};
    using LogPrintf = void (*)(const char*, ...);
    LogPrintf logprintf = nullptr;

    std::uint64_t nowMs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    bool validPlayer(int playerid)
    {
        return playerid >= 0 && playerid < MaxPlayers;
    }

    float cellToFloat(cell value)
    {
        float result = 0.0f;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    bool finitePosition(float x, float y, float z)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z)
            && std::fabs(x) <= 20000.0f
            && std::fabs(y) <= 20000.0f
            && z >= -2000.0f && z <= 5000.0f;
    }

    void resetState(PlayerState& state, std::uint64_t now)
    {
        const bool enabled = state.enabled;
        state = PlayerState{};
        state.enabled = enabled;
        state.graceUntil = now + TeleportGraceMs;
        state.lastDecayAt = now;
    }

    cell AMX_NATIVE_CALL DG_CheckPlayer(AMX*, cell* params)
    {
        if (params[0] < 12 * static_cast<cell>(sizeof(cell)))
            return ViolationNone;

        const int playerid = static_cast<int>(params[1]);
        if (!validPlayer(playerid))
            return ViolationNone;

        PlayerState& state = players[static_cast<std::size_t>(playerid)];
        if (!state.enabled)
            return ViolationNone;

        const float x = cellToFloat(params[2]);
        const float y = cellToFloat(params[3]);
        const float z = cellToFloat(params[4]);
        const float health = cellToFloat(params[5]);
        const float armour = cellToFloat(params[6]);
        const int weapon = static_cast<int>(params[7]);
        const int ammo = static_cast<int>(params[8]);
        const int vehicleId = static_cast<int>(params[9]);
        const int interiorId = static_cast<int>(params[10]);
        const int worldId = static_cast<int>(params[11]);
        const int specialAction = static_cast<int>(params[12]);
        const std::uint64_t now = nowMs();

        cell flags = ViolationNone;
        const bool positionValid = finitePosition(x, y, z);

        if (!positionValid)
            flags |= ViolationPosition;

        if (!std::isfinite(health) || health < 0.0f || health > 100.5f)
            flags |= ViolationHealth;

        if (!std::isfinite(armour) || armour < 0.0f || armour > 100.5f)
            flags |= ViolationArmour;

        if (weapon < 0 || weapon > 46)
            flags |= ViolationWeapon;

        if (ammo < 0 || ammo > 10000)
            flags |= ViolationAmmo;

        const bool inGrace = now < state.graceUntil;
        const bool sameWorld = state.worldId == worldId && state.interiorId == interiorId;
        const bool sameVehicle = state.vehicleId == vehicleId;
        bool updateBaseline = !state.initialized || inGrace || !sameWorld || !sameVehicle;

        if (positionValid && state.initialized && !inGrace && sameWorld && sameVehicle)
        {
            const std::uint64_t elapsed = now - state.lastUpdate;
            if (elapsed >= 100 && elapsed <= 4000)
            {
                const double dx = static_cast<double>(x) - state.x;
                const double dy = static_cast<double>(y) - state.y;
                const double dz = static_cast<double>(z) - state.z;
                const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double seconds = static_cast<double>(elapsed) / 1000.0;
                const double allowedDistance = vehicleId == 0
                    ? (specialAction == 2 ? 50.0 + 60.0 * seconds : 8.0 + 16.0 * seconds)
                    : 55.0 + 330.0 * seconds;

                if (distance > allowedDistance)
                    flags |= ViolationMovement;
            }

            if (elapsed >= 100)
                updateBaseline = true;
        }

        if (positionValid && updateBaseline)
        {
            state.x = x;
            state.y = y;
            state.z = z;
            state.lastUpdate = now;
            state.vehicleId = vehicleId;
            state.interiorId = interiorId;
            state.worldId = worldId;
            state.initialized = true;
        }

        if (flags != ViolationNone)
        {
            state.lastViolation = flags;
            if (now - state.lastViolationAt >= ViolationCooldownMs)
            {
                state.violationLevel = std::min(MaxViolationLevel, state.violationLevel + 1);
                state.lastViolationAt = now;
                if (logprintf)
                {
                    logprintf("[DrayvenGUARD] player %d violation flags=%d level=%d/%d",
                        playerid, static_cast<int>(flags), state.violationLevel, KickViolationLevel);
                }
            }
        }
        else if (state.violationLevel > 0 && now - state.lastDecayAt >= CleanDecayMs)
        {
            --state.violationLevel;
            state.lastDecayAt = now;
        }

        return flags;
    }

    cell AMX_NATIVE_CALL DG_ResetPlayer(AMX*, cell* params)
    {
        const int playerid = static_cast<int>(params[1]);
        if (validPlayer(playerid))
            resetState(players[static_cast<std::size_t>(playerid)], nowMs());
        return 1;
    }

    cell AMX_NATIVE_CALL DG_AllowTeleport(AMX*, cell* params)
    {
        const int playerid = static_cast<int>(params[1]);
        if (!validPlayer(playerid))
            return 0;

        int duration = params[0] >= 2 * static_cast<cell>(sizeof(cell))
            ? static_cast<int>(params[2]) : static_cast<int>(TeleportGraceMs);
        duration = std::clamp(duration, 0, 30000);

        PlayerState& state = players[static_cast<std::size_t>(playerid)];
        state.graceUntil = nowMs() + static_cast<std::uint64_t>(duration);
        state.initialized = false;
        return 1;
    }

    cell AMX_NATIVE_CALL DG_GetViolationLevel(AMX*, cell* params)
    {
        const int playerid = static_cast<int>(params[1]);
        return validPlayer(playerid)
            ? players[static_cast<std::size_t>(playerid)].violationLevel
            : 0;
    }

    cell AMX_NATIVE_CALL DG_GetLastViolation(AMX*, cell* params)
    {
        const int playerid = static_cast<int>(params[1]);
        return validPlayer(playerid)
            ? players[static_cast<std::size_t>(playerid)].lastViolation
            : ViolationNone;
    }

    cell AMX_NATIVE_CALL DG_SetEnabled(AMX*, cell* params)
    {
        if (params[0] < 2 * static_cast<cell>(sizeof(cell)))
            return 0;

        const int playerid = static_cast<int>(params[1]);
        if (!validPlayer(playerid))
            return 0;

        players[static_cast<std::size_t>(playerid)].enabled = params[2] != 0;
        players[static_cast<std::size_t>(playerid)].initialized = false;
        return 1;
    }

    AMX_NATIVE_INFO natives[] =
    {
        { "DG_CheckPlayer", DG_CheckPlayer },
        { "DG_ResetPlayer", DG_ResetPlayer },
        { "DG_AllowTeleport", DG_AllowTeleport },
        { "DG_GetViolationLevel", DG_GetViolationLevel },
        { "DG_GetLastViolation", DG_GetLastViolation },
        { "DG_SetEnabled", DG_SetEnabled },
        { nullptr, nullptr }
    };
}

void* pAMXFunctions = nullptr;

int AMXAPI amx_Register(AMX* amx, const AMX_NATIVE_INFO* list, int number)
{
    if (!pAMXFunctions)
        return AMX_ERR_PARAMS;

    using RegisterFunction = int (AMXAPI*)(AMX*, const AMX_NATIVE_INFO*, int);
    auto* table = static_cast<void**>(pAMXFunctions);
    auto function = reinterpret_cast<RegisterFunction>(table[PLUGIN_AMX_EXPORT_Register]);
    return function ? function(amx, list, number) : AMX_ERR_PARAMS;
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports()
{
    return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** data)
{
    if (!data)
        return false;

    pAMXFunctions = data[PLUGIN_DATA_AMX_EXPORTS];
    logprintf = reinterpret_cast<LogPrintf>(data[PLUGIN_DATA_LOGPRINTF]);

    if (!pAMXFunctions)
        return false;

    if (logprintf)
        logprintf("[DrayvenGUARD] C++ anti-cheat loaded for SA-MP/open.mp.");

    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload()
{
    if (logprintf)
        logprintf("[DrayvenGUARD] unloaded.");

    pAMXFunctions = nullptr;
    logprintf = nullptr;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* amx)
{
    return amx_Register(amx, natives, -1);
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX*)
{
    return AMX_ERR_NONE;
}
