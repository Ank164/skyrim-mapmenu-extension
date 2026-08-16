#ifndef UTILITY_H
#define UTILITY_H

#include <nlohmann/json.hpp>
#include <unordered_set>
#include <unordered_map>
#include <SimpleIni.h>
#include <functional>

namespace logger = SKSE::log;

extern int menuHotkey;
extern int regionsKey;
extern int mapModeKey;
extern int leftKey;
extern int rightKey;
extern int menuGamepadHotkey;
extern int regionsGamepadKey;
extern int mapModeGamepadKey;
extern int leftGamepadKey;
extern int rightGamepadKey;
extern bool enableWarmup;

extern bool g_isUltraWide;
extern bool g_mapWorldOverrideActive;

static const std::string INI_FILE_PATH = "Data/Map Menu Extension.ini";

namespace RE::BSModelDB {
    struct DBTraits {
        struct ArgsType {
            std::uint32_t LODmult{0};
            std::uint32_t texLoadLevel{3};
            bool unk8{true};
            bool unk9{false};
            bool unkA{true};
            bool postProcess{true};
        };
    };

    BSResource::ErrorCode Demand(const char* a_modelPath, RE::NiPointer<RE::NiNode>& a_modelOut,
                                 const DBTraits::ArgsType& a_args);
}

std::uint32_t GamepadKeycodeToMask(std::int32_t keyCode);

struct WorldspaceBackup {
    RE::WORLD_MAP_DATA worldMapData;
    RE::NiPoint2 minimumCoords;
    RE::NiPoint2 maximumCoords;
    RE::BGSLocation* location;
    float lodWaterHeight;
    std::int8_t* maxHeightData;
    RE::TESWaterForm* lodWater;
    RE::TESFileContainer sourceFiles;
    RE::TESWorldSpace* parentWorld;
    RE::BSTArray<RE::ObjectRefHandle> mapMarkers;
    RE::BSResource::ID* textures;
    decltype(RE::TESWorldSpace::parentUseFlags) parentUseFlags;
};

struct CrimeFactionInfo {
    int crimeGold{0};
    std::string crimeType;  // "", "petty", "arrest", "attack"
};

extern bool pendingWorldSwitch;
extern bool pendingMapReopen;
extern nlohmann::json worldMapsData;
extern nlohmann::json tacticalRegionsData;
extern std::unordered_map<std::string, nlohmann::json> tacticalFactionsByID;
extern std::unordered_map<std::string, nlohmann::json> tacticalClimatesByID;
extern WorldspaceBackup backupWorld;
extern RE::TESWorldSpace* backup;
extern bool cloudSwapPending;
extern RE::BSSimpleList<RE::BGSQuestObjective*> hiddenObjectives;

void SetupLog();
void LoadDataFromINI();
bool HasStringField(const nlohmann::json& data, const char* field);
CrimeFactionInfo GetCrimeFactionInfo(const std::string& factionId);
bool IsSeasonsInstalled();
float GetLocationKeywordData(const RE::BGSLocation* location, const RE::BGSKeyword* keyword);
float GetGlobalValue(const std::string& editorID);
std::uint32_t GetCurrentSeason();
std::string ResolveRegionFactionID(const nlohmann::json& regionData);
void UpdateQuestMarkers(RE::TESWorldSpace* targetWorld);
RE::BSTArray<RE::ObjectRefHandle> GetMapMarkersInWorld(RE::TESWorldSpace* targetWorld);
std::pair<int, int> GetDiscoverStatsForWorld(RE::TESWorldSpace* targetWorld);
void BackupCurrentWorld(RE::TESWorldSpace* currentWorld);
std::pair<int, int> GetQuestStatsForWorld(RE::TESWorldSpace* targetWorld);
void RestoreWorldspaceState(RE::TESWorldSpace* world, bool restoreClouds = true);
void DisplaySelectedWorld(RE::TESWorldSpace* a_current, RE::TESWorldSpace* a_target);
bool IsCellInWorld(RE::BGSLocation* a_loc, RE::TESWorldSpace* a_world);
bool IsMarkerInWorld(RE::TESWorldSpace* a_refWorld, RE::TESWorldSpace* a_targetWorld);
void SetINIValue(const char* settingName, float value);
void LoadWorldMaps();
void LoadTacticalData();
bool GetMapCenterOverride(RE::TESWorldSpace* world, RE::NiPoint3& position);
bool IsQuestReferenceInWorld(RE::TESObjectREFR* ref, RE::TESWorldSpace* targetWorld);

#endif  // UTILITY_H