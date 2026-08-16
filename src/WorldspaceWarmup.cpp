#include "WorldspaceWarmup.h"

#include "Utility.h"
#include <unordered_set>

using GetMapData_t = void* (*)(RE::TESWorldSpace*);
using SetActive_t = std::int64_t (*)(void*, bool, bool);

static inline REL::Relocation<GetMapData_t> GetMapData{REL::ID(20469)};

static inline REL::Relocation<SetActive_t> SetActive{REL::ID(31818)};

bool WarmWorldspace(RE::TESWorldSpace* world) {
    if (!world) {
        logger::error("[WorldspaceWarmup] Null worldspace");
        return false;
    }

    if (!enableWarmup) {
        logger::info("[WorldspaceWarmup] Warmup disabled by INI settings, skipping");
        return false;
    }

    auto* mapData = GetMapData(world);

    if (!mapData) {
        logger::error("[WorldspaceWarmup] '{}' has no map data", world->GetName());

        return false;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(mapData);

    const bool active = *reinterpret_cast<const std::uint8_t*>(address) != 0;

    const bool activatable = *reinterpret_cast<const std::uint8_t*>(address + 0x36) != 0;

    logger::info(
        "[WorldspaceWarmup] '{}' mapData={:X} "
        "active={} activatable={}",
        world->GetName(), address, active, activatable);

    if (!activatable) {
        logger::error("[WorldspaceWarmup] '{}' is not activatable", world->GetName());

        return false;
    }

    if (active) {
        logger::info("[WorldspaceWarmup] '{}' already active; skipped", world->GetName());

        return true;
    }

    SetActive(mapData, true, false);

    logger::info("[WorldspaceWarmup] Activated '{}'", world->GetName());

    SetActive(mapData, false, true);

    const bool activeAfter = *reinterpret_cast<const std::uint8_t*>(address) != 0;

    if (activeAfter) {
        logger::error("[WorldspaceWarmup] '{}' remained active", world->GetName());

        return false;
    }

    logger::info("[WorldspaceWarmup] Deactivated '{}'", world->GetName());

    return true;
}

std::vector<RE::TESWorldSpace*> GetWorldspacesMarkedForWarmup() {
    std::vector<RE::TESWorldSpace*> result;

    auto* dataHandler = RE::TESDataHandler::GetSingleton();

    if (!dataHandler) {
        logger::error(
            "[WorldspaceWarmup] Cannot resolve configured worlds: "
            "TESDataHandler unavailable");

        return result;
    }

    std::unordered_set<RE::FormID> addedWorlds;

    for (const auto& worldData : worldMapsData) {
        if (worldData.value("warmupWorld", 0) != 1) {
            continue;
        }

        const auto configuredName = worldData.value("worldName", std::string{"<unnamed>"});

        if (!worldData.contains("worldID") || !worldData["worldID"].is_string()) {
            logger::error(
                "[WorldspaceWarmup] Cannot queue '{}': "
                "warmupWorld=1 but worldID is missing or invalid",
                configuredName);

            continue;
        }

        if (!worldData.contains("plugin") || !worldData["plugin"].is_string()) {
            logger::error(
                "[WorldspaceWarmup] Cannot queue '{}': "
                "warmupWorld=1 but plugin is missing or invalid",
                configuredName);

            continue;
        }

        const auto pluginName = worldData["plugin"].get<std::string>();

        RE::FormID localFormID{};

        try {
            localFormID = static_cast<RE::FormID>(std::stoul(worldData["worldID"].get<std::string>(), nullptr, 16));
        } catch (const std::exception& exception) {
            logger::error(
                "[WorldspaceWarmup] Cannot queue '{}': "
                "invalid worldID '{}': {}",
                configuredName, worldData.value("worldID", "<missing>"), exception.what());

            continue;
        }

        auto* world = dataHandler->LookupForm<RE::TESWorldSpace>(localFormID, pluginName);

        if (!world) {
            logger::error(
                "[WorldspaceWarmup] Cannot queue '{}': "
                "{}|{:06X} did not resolve",
                configuredName, pluginName, localFormID);

            continue;
        }

        if (!addedWorlds.insert(world->GetFormID()).second) {
            logger::info(
                "[WorldspaceWarmup] Duplicate warm-up entry '{}' "
                "skipped (formID={:08X})",
                configuredName, world->GetFormID());

            continue;
        }

        result.push_back(world);

        logger::info(
            "[WorldspaceWarmup] Queued '{}' "
            "(runtime='{}', formID={:08X}, plugin='{}')",
            configuredName, world->GetName() ? world->GetName() : "<unnamed>", world->GetFormID(), pluginName);
    }

    logger::info("[WorldspaceWarmup] {} worldspace(s) marked for warm-up", result.size());

    return result;
}