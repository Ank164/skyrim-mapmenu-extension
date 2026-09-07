#include "Utility.h"
#include "Hooks.h"
#include "WorldspaceWarmup.h"

#include <Windows.h>
#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <fstream>
#include <unordered_map>

int menuHotkey;
int regionsKey;
int mapModeKey;
int leftKey;
int rightKey;
int menuGamepadHotkey;
int regionsGamepadKey;
int mapModeGamepadKey;
int leftGamepadKey;
int rightGamepadKey;
bool enableWarmup = true;

bool g_isUltraWide = false;

using GetCurrentSeason_t = std::uint32_t (*)();

namespace fs = std::filesystem;
namespace logger = SKSE::log;
bool pendingWorldSwitch = false;
bool pendingMapReopen = false;
bool g_mapWorldOverrideActive = false;
WorldspaceBackup backupWorld;
RE::TESWorldSpace* backup;
RE::BSSimpleList<RE::BGSQuestObjective*> hiddenObjectives;

const std::string WORLD_MAPS_DIRECTORY = "DATA/interface/worldmaps";
nlohmann::json worldMapsData = nlohmann::json::array();

const std::string TACTICAL_FACTIONS_DIRECTORY = "DATA/interface/worldmaps/tactical/factions";
const std::string TACTICAL_REGIONS_DIRECTORY = "DATA/interface/worldmaps/tactical/regions";
constexpr auto TACTICAL_CLIMATES_DIRECTORY = "DATA/interface/worldmaps/tactical/climates";
nlohmann::json tacticalRegionsData = nlohmann::json::array();
std::unordered_map<std::string, nlohmann::json> tacticalFactionsByID;
std::unordered_map<std::string, nlohmann::json> tacticalClimatesByID;

bool cloudSwapPending = false;

std::uint32_t GamepadKeycodeToMask(std::int32_t keyCode) {
    switch (keyCode) {
        case 266:
            return 0x0001;  // D-pad Up
        case 267:
            return 0x0002;  // D-pad Down
        case 268:
            return 0x0004;  // D-pad Left
        case 269:
            return 0x0008;  // D-pad Right
        case 270:
            return 0x0010;  // Start
        case 271:
            return 0x0020;  // Back
        case 272:
            return 0x0040;  // L3
        case 273:
            return 0x0080;  // R3
        case 274:
            return 0x0100;  // LB
        case 275:
            return 0x0200;  // RB
        case 276:
            return 0x1000;  // A
        case 277:
            return 0x2000;  // B
        case 278:
            return 0x4000;  // X
        case 279:
            return 0x8000;  // Y
        default:
            return static_cast<std::uint32_t>(keyCode);
    }
}

namespace RE::BSModelDB {
    BSResource::ErrorCode Demand(const char* a_modelPath, RE::NiPointer<RE::NiNode>& a_modelOut,
                                 const DBTraits::ArgsType& a_args) {
        using func_t = BSResource::ErrorCode (*)(const char*, RE::NiPointer<RE::NiNode>&, const DBTraits::ArgsType&);
        static REL::Relocation<func_t> func{RELOCATION_ID(74040, 75782)};
        return func(a_modelPath, a_modelOut, a_args);
    }
}

namespace {
    struct CachedMapMarker {
        RE::TESObjectREFR* ref{nullptr};
        RE::MapMarkerData* data{nullptr};
        RE::TESWorldSpace* world{nullptr};
    };

    std::vector<CachedMapMarker> g_allMapMarkers;

    std::unordered_map<RE::FormID, std::vector<const CachedMapMarker*>> g_mapMarkersByWorld;

    bool g_mapMarkerCacheBuilt = false;

    bool IsDisabledMapMarkerType(const RE::MapMarkerData* data) {
        if (!data) {
            return false;
        }

        return data->type == RE::MARKER_TYPE::kDLC02ToSkyrim || data->type == RE::MARKER_TYPE::kDLC02ToSolstheim ||
               data->type.underlying() == 217; //217 is BS:Bruma's "To Skyrim" marker type
    }

    void BuildMapMarkerCache() {
        if (g_mapMarkerCacheBuilt) {
            return;
        }

        const auto [formMap, formMapLock] = RE::TESForm::GetAllForms();
        if (!formMap) {
            return;
        }

        RE::BSReadLockGuard lock{formMapLock.get()};

        g_allMapMarkers.clear();
        g_mapMarkersByWorld.clear();
        g_allMapMarkers.reserve(1024);

        for (const auto& [formID, form] : *formMap) {
            if (!form || !form->Is(RE::FormType::Reference)) {
                continue;
            }

            auto* ref = form->As<RE::TESObjectREFR>();
            if (!ref || !ref->GetBaseObject()) {
                continue;
            }

            auto* marker = ref->extraList.GetByType<RE::ExtraMapMarker>();
            
            if (!marker || !marker->mapData) {
                continue;
            }

            auto* markerData = marker->mapData;
            
            if (IsDisabledMapMarkerType(markerData)) {
                const auto markerType = static_cast<unsigned int>(markerData->type.underlying());

                markerData->flags.reset(RE::MapMarkerData::Flag::kVisible);
                markerData->flags.reset(RE::MapMarkerData::Flag::kCanTravelTo);

                continue;
            }

            auto* world = ref->GetWorldspace();

            if (!world) {
                continue;
            }

            g_allMapMarkers.push_back({ref, markerData, world});
        }

        g_mapMarkerCacheBuilt = true;

        logger::debug("Cached {} world map markers", g_allMapMarkers.size());
    }

    const std::vector<const CachedMapMarker*>& GetCachedMarkersForWorld(RE::TESWorldSpace* targetWorld) {
        const auto worldID = targetWorld->GetFormID();

        if (const auto it = g_mapMarkersByWorld.find(worldID); it != g_mapMarkersByWorld.end()) {
            return it->second;
        }

        auto [it, inserted] = g_mapMarkersByWorld.try_emplace(worldID);

        auto& result = it->second;
        result.reserve(g_allMapMarkers.size() / 8);

        for (const auto& marker : g_allMapMarkers) {
            if (IsMarkerInWorld(marker.world, targetWorld)) {
                result.push_back(std::addressof(marker));
            }
        }

        return result;
    }
}

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
}

bool HasStringField(const nlohmann::json& data, const char* field) {
    return data.contains(field) && data[field].is_string();
}

float GetLocationKeywordData(const RE::BGSLocation* location, const RE::BGSKeyword* keyword) {
    if (!location || !keyword) {
        return 0.0f;
    }

    for (const auto& entry : location->keywordData) {
        if (entry.keyword == keyword) {
            return entry.data;
        }
    }

    return 0.0f;
}

float GetGlobalValue(const std::string& editorID) {
    auto* global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(editorID);

    if (!global) {
        logger::warn("Could not find global '{}'", editorID);
        return 0.0f;
    }

    return global->value;
}

CrimeFactionInfo GetCrimeFactionInfo(const std::string& factionId) {
    CrimeFactionInfo result{};

    auto* crimeFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(factionId);

    if (!crimeFaction) {
        logger::warn("Could not find crime faction '{}'", factionId);
        return result;
    }

    result.crimeGold = crimeFaction->GetCrimeGold();

    const int pettyThreshold = static_cast<int>(GetGlobalValue("CrimePettyCrimeThreshold"));

    const int arrestThreshold = static_cast<int>(GetGlobalValue("CrimeArrestOnSightNonViolentThreshold"));

    if (result.crimeGold == 0) {
        result.crimeType = "";
    } else if (result.crimeGold < pettyThreshold) {
        result.crimeType = "petty";
    } else if (result.crimeGold < arrestThreshold) {
        result.crimeType = "arrest";
    } else {
        result.crimeType = "attack";
    }

    logger::trace("Crime faction '{}': gold={}, type={}", factionId, result.crimeGold, result.crimeType);

    return result;
}

std::uint32_t GetCurrentSeason() {
    const auto module = GetModuleHandleW(L"po3_SeasonsOfSkyrim.dll");

    if (!module) {
        return 3;
    }

    const auto function = reinterpret_cast<GetCurrentSeason_t>(GetProcAddress(module, "GetCurrentSeason"));

    if (!function) {
        logger::warn("SeasonsOfSkyrim.dll does not export GetCurrentSeason");
        return 3;
    }

    return function();
}

bool IsSeasonsInstalled() {
    const auto module = GetModuleHandleW(L"po3_SeasonsOfSkyrim.dll");

    if (!module) {
        return false;
    }

    return true;
}

std::string ResolveRegionFactionID(const nlohmann::json& regionData) {
    std::string factionID = regionData.value("factionId", "");

    if (!HasStringField(regionData, "loc") || !HasStringField(regionData, "keyword") ||
        !regionData.contains("switch") || !regionData["switch"].is_object()) {
        return factionID;
    }

    const std::string locationEditorID = regionData["loc"].get<std::string>();

    const std::string keywordEditorID = regionData["keyword"].get<std::string>();

    auto* location = RE::TESForm::LookupByEditorID<RE::BGSLocation>(locationEditorID);

    auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(keywordEditorID);

    if (!location || !keyword) {
        logger::warn("Could not resolve tactical location '{}' or keyword '{}'", locationEditorID, keywordEditorID);

        return factionID;
    }

    const float value = GetLocationKeywordData(location, keyword);

    const std::string switchKey = std::to_string(static_cast<std::int32_t>(value));

    const auto& switchData = regionData["switch"];

    const auto switchIt = switchData.find(switchKey);

    if (switchIt != switchData.end() && switchIt->is_string()) {
        return switchIt->get<std::string>();
    }

    return factionID;
}

void LoadDataFromINI() {
    CSimpleIniA ini;
    ini.SetUnicode();
    SI_Error rc = ini.LoadFile(INI_FILE_PATH.c_str());
    if (rc < 0) {
        logger::error("Failed to load INI file: {}", INI_FILE_PATH);
        return;
    }

    const char* keycodeStr = ini.GetValue("General", "iOpenMenuKeycode", "57");     // Spacebar
    const char* regionsStr = ini.GetValue("General", "iShowRegionsKeycode", "19");  // R
    const char* modeStr = ini.GetValue("General", "iMapModeKeycode", "16");         // Q
    const char* leftStr = ini.GetValue("General", "iLeftKeycode", "30");            // A
    const char* rightStr = ini.GetValue("General", "iRightKeycode", "32");          // D

    const char* keycodeGamepadStr = ini.GetValue("General", "iOpenMenuGamepadKeycode", "272");     // LS
    const char* regionsGamepadStr = ini.GetValue("General", "iShowRegionsGamepadKeycode", "279");  // Y
    const char* modeGamepadStr = ini.GetValue("General", "iMapModeGamepadKeycode", "278");         // X
    const char* leftGamepadStr = ini.GetValue("General", "iLeftGamepadKeycode", "274");            // LB
    const char* rightGamepadStr = ini.GetValue("General", "iRightGamepadKeycode", "275");          // RB

    enableWarmup = ini.GetBoolValue("MapMenu", "bEnableWorldspaceWarmup", true);

    menuHotkey = std::stoi(keycodeStr);
    regionsKey = std::stoi(regionsStr);
    mapModeKey = std::stoi(modeStr);
    leftKey = std::stoi(leftStr);
    rightKey = std::stoi(rightStr);

    menuGamepadHotkey = std::stoi(keycodeGamepadStr);
    regionsGamepadKey = std::stoi(regionsGamepadStr);
    mapModeGamepadKey = std::stoi(modeGamepadStr);
    leftGamepadKey = std::stoi(leftGamepadStr);
    rightGamepadKey = std::stoi(rightGamepadStr);
}

RE::BSTArray<RE::ObjectRefHandle> GetMapMarkersInWorld(RE::TESWorldSpace* targetWorld) {
    RE::BSTArray<RE::ObjectRefHandle> handles;

    if (!targetWorld) {
        return handles;
    }

    BuildMapMarkerCache();

    const auto& markers = GetCachedMarkersForWorld(targetWorld);

    for (const auto* marker : markers) {
        if (!marker || !marker->ref || !marker->data) {
            continue;
        }

        if (IsDisabledMapMarkerType(marker->data)) {
            continue;
        }

        if (marker->ref->IsDisabled()) {
            continue;
        }

        if (!marker->data->flags.all(RE::MapMarkerData::Flag::kVisible)) {
            continue;
        }

        RE::ObjectRefHandle handle;
        handle = marker->ref;
        handles.push_back(handle);

        logger::debug("MARKER {}, type: {}", marker->data->locationName.GetFullName(), marker->data->type.underlying());
    }

    logger::trace("Found {} visible map markers in {} world.", handles.size(), targetWorld->GetName());

    return handles;
}

std::pair<int, int> GetDiscoverStatsForWorld(RE::TESWorldSpace* targetWorld) {
    if (!targetWorld) {
        return {0, 0};
    }

    BuildMapMarkerCache();

    int discovered = 0;
    int total = 0;

    const auto& markers = GetCachedMarkersForWorld(targetWorld);

    for (const auto* marker : markers) {
        if (!marker || !marker->ref || !marker->data) {
            continue;
        }

        if (IsDisabledMapMarkerType(marker->data)) {
            continue;
        }

        if (marker->ref->IsDisabled()) {
            continue;
        }

        ++total;

        if (marker->data->flags.all(RE::MapMarkerData::Flag::kCanTravelTo)) {
            ++discovered;
        }
    }

    return {discovered, total};
}

bool IsQuestReferenceInWorld(RE::TESObjectREFR* ref, RE::TESWorldSpace* targetWorld) {
    if (!ref || !targetWorld) {
        return false;
    }

    if (auto* world = ref->GetWorldspace()) {
        return IsMarkerInWorld(world, targetWorld);
    }

    auto* cell = ref->GetParentCell();

    if (!cell) {
        return false;
    }

    auto* location = cell->GetLocation();

    if (!location) {
        return false;
    }

    if (targetWorld->location) {
        return IsCellInWorld(location, targetWorld);
    }

    for (auto* current = location; current; current = current->parentLoc) {
        if (current->GetFormID() == 0x000130FF) {
            return targetWorld->GetFormID() == 0x0000003C;
        }
    }

    return false;
}

using UniqueActorRefMap = std::unordered_map<RE::TESNPC*, std::vector<RE::TESObjectREFR*>>;

UniqueActorRefMap BuildUniqueActorRefMap() {
    UniqueActorRefMap result;

    const auto& [forms, lock] = RE::TESForm::GetAllForms();
    const RE::BSReadWriteLock guard{lock};

    if (!forms) {
        return result;
    }

    for (const auto& [_, form] : *forms) {
        if (!form) {
            continue;
        }

        auto* actor = form->As<RE::Actor>();
        if (!actor) {
            continue;
        }

        auto* npc = actor->GetActorBase();
        if (!npc) {
            continue;
        }

        result[npc].push_back(actor);
    }

    return result;
}

RE::BGSRefAlias* FindQuestRefAlias(RE::TESQuest* quest, std::uint32_t aliasID) {
    if (!quest) {
        return nullptr;
    }

    for (auto* baseAlias : quest->aliases) {
        if (!baseAlias || baseAlias->aliasID != aliasID) {
            continue;
        }

        return skyrim_cast<RE::BGSRefAlias*>(baseAlias);
    }

    return nullptr;
}

bool QuestAliasPointsToWorld(RE::TESQuest* quest, RE::BGSRefAlias* alias, RE::TESWorldSpace* targetWorld,
                             const UniqueActorRefMap& uniqueActorRefs, std::unordered_set<std::uint64_t>& visited,
                             std::uint32_t depth = 0) {
    if (!quest || !alias || !targetWorld || depth > 16) {
        return false;
    }

    const std::uint64_t key =
        (static_cast<std::uint64_t>(quest->GetFormID()) << 32) | static_cast<std::uint64_t>(alias->aliasID);

    if (!visited.insert(key).second) {
        return false;
    }

    if (auto* ref = alias->GetReference()) {
        return IsQuestReferenceInWorld(ref, targetWorld);
    }

    const auto fillType = static_cast<RE::BGSBaseAlias::FILL_TYPE>(alias->fillType.underlying());

    switch (fillType) {
        case RE::BGSBaseAlias::FILL_TYPE::kForced: {
            auto forcedRef = alias->fillData.forced.forcedRef.get();

            return forcedRef && IsQuestReferenceInWorld(forcedRef.get(), targetWorld);
        }

        case RE::BGSBaseAlias::FILL_TYPE::kFromAlias: {
            auto* sourceAlias = FindQuestRefAlias(quest, alias->fillData.fromAlias.forcedFromAlias);

            return sourceAlias &&
                   QuestAliasPointsToWorld(quest, sourceAlias, targetWorld, uniqueActorRefs, visited, depth + 1);
        }

        case RE::BGSBaseAlias::FILL_TYPE::kCreated: {
            auto* sourceAlias =
                FindQuestRefAlias(quest, static_cast<std::uint32_t>(alias->fillData.created.alias.alias));

            return sourceAlias &&
                   QuestAliasPointsToWorld(quest, sourceAlias, targetWorld, uniqueActorRefs, visited, depth + 1);
        }

        case RE::BGSBaseAlias::FILL_TYPE::kFromExternal: {
            auto* externalQuest = alias->fillData.fromExternal.externalQuest;

            if (!externalQuest) {
                return false;
            }

            auto* externalAlias = FindQuestRefAlias(externalQuest, alias->fillData.fromExternal.externalAlias);

            return externalAlias && QuestAliasPointsToWorld(externalQuest, externalAlias, targetWorld, uniqueActorRefs,
                                                            visited, depth + 1);
        }

        case RE::BGSBaseAlias::FILL_TYPE::kUniqueActor: {
            auto* npc = alias->fillData.uniqueActor.uniqueActor;

            if (!npc) {
                return false;
            }

            const auto it = uniqueActorRefs.find(npc);

            if (it == uniqueActorRefs.end()) {
                return false;
            }

            for (auto* ref : it->second) {
                if (ref && IsQuestReferenceInWorld(ref, targetWorld)) {
                    return true;
                }
            }

            return false;
        }

        case RE::BGSBaseAlias::FILL_TYPE::kNearAlias: {
            auto* sourceAlias = FindQuestRefAlias(quest, alias->fillData.nearAlias.nearAlias);

            return sourceAlias &&
                   QuestAliasPointsToWorld(quest, sourceAlias, targetWorld, uniqueActorRefs, visited, depth + 1);
        }

        case RE::BGSBaseAlias::FILL_TYPE::kConditions:
        case RE::BGSBaseAlias::FILL_TYPE::kFromEvent:
        default:
            return false;
    }
}

void UpdateQuestMarkers(RE::TESWorldSpace* targetWorld) {
    if (!targetWorld) {
        logger::warn("[QuestMarkers] No targetWorld");
        return;
    }

    logger::trace("[QuestMarkers] Updating for world '{}' {:08X}",
                 targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>", targetWorld->GetFormID());

    // Restore objectives hidden by previous remote-world filtering.
    std::size_t restored = 0;

    for (auto* objective : hiddenObjectives) {
        if (objective) {
            objective->state.set(RE::QUEST_OBJECTIVE_STATE::kDisplayed);

            ++restored;
        }
    }

    hiddenObjectives.clear();

    logger::info("[QuestMarkers] Restored {} previously hidden objectives", restored);

    auto* dataHandler = RE::TESDataHandler::GetSingleton();

    if (!dataHandler) {
        logger::error("[QuestMarkers] TESDataHandler unavailable");
        return;
    }

    const auto& quests = dataHandler->GetFormArray(RE::FormType::Quest);

    std::size_t activeObjectives = 0;
    std::size_t objectivesWithTargets = 0;
    std::size_t resolvedRefs = 0;
    std::size_t unresolvedRefs = 0;
    std::size_t matchingObjectives = 0;
    std::size_t hiddenCount = 0;

    for (auto* form : quests) {
        auto* quest = form ? form->As<RE::TESQuest>() : nullptr;

        if (!quest || quest->IsCompleted()) {
            continue;
        }

        for (auto* objective : quest->objectives) {
            if (!objective) {
                continue;
            }

            const bool displayed = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kDisplayed);

            const bool completed = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kCompleted);

            const bool failed = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kFailed);

            if (!displayed || completed || failed) {
                continue;
            }

            ++activeObjectives;

            const char* questName = quest->GetName() ? quest->GetName() : "<unnamed quest>";

            const char* objectiveText =
                objective->displayText.c_str() ? objective->displayText.c_str() : "<no objective text>";

            logger::trace("[QuestMarkers] ACTIVE quest='{}' {:08X} objective='{}' targets={}", questName,
                         quest->GetFormID(), objectiveText, objective->numTargets);

            if (objective->numTargets > 0) {
                ++objectivesWithTargets;
            }

            bool markerInWorld = false;

            for (std::uint32_t i = 0; i < objective->numTargets; ++i) {
                auto* target = objective->targets[i];

                if (!target) {
                    logger::warn("[QuestMarkers]   target[{}] = null", i);

                    continue;
                }

                logger::trace("[QuestMarkers]   target[{}] aliasID={}", i, static_cast<std::uint32_t>(target->alias));

                auto* alias = FindQuestRefAlias(quest, target->alias);

                if (!alias) {
                    logger::warn("[QuestMarkers]   alias {} NOT FOUND in quest '{}'",
                                 static_cast<std::uint32_t>(target->alias), questName);

                    ++unresolvedRefs;
                    continue;
                }

                auto* ref = alias->GetReference();

                if (!ref) {
                    logger::warn("[QuestMarkers]   alias {} has NO live reference",
                                 static_cast<std::uint32_t>(target->alias));

                    ++unresolvedRefs;
                    continue;
                }

                ++resolvedRefs;

                auto* refWorld = ref->GetWorldspace();
                auto* cell = ref->GetParentCell();
                auto* location = cell ? cell->GetLocation() : nullptr;

                logger::trace("[QuestMarkers]   ref={:08X} name='{}' world='{}' {:08X} cell={:08X} location='{}'",
                             ref->GetFormID(), ref->GetName() ? ref->GetName() : "<unnamed>",
                             refWorld && refWorld->GetName() ? refWorld->GetName() : "<none>",
                             refWorld ? refWorld->GetFormID() : 0, cell ? cell->GetFormID() : 0,
                             location && location->GetName() ? location->GetName() : "<none>");

                const bool inWorld = IsQuestReferenceInWorld(ref, targetWorld);

                logger::trace("[QuestMarkers]   -> in target world = {}", inWorld ? "YES" : "NO");

                if (inWorld) {
                    markerInWorld = true;
                    break;
                }
            }

            if (markerInWorld) {
                ++matchingObjectives;

                logger::trace("[QuestMarkers] KEEP quest='{}' objective='{}'", questName, objectiveText);

                continue;
            }

            logger::trace("[QuestMarkers] HIDE quest='{}' objective='{}'", questName, objectiveText);

            objective->state.reset(RE::QUEST_OBJECTIVE_STATE::kDisplayed);

            hiddenObjectives.push_front(objective);

            ++hiddenCount;
        }
    }

    auto* player = RE::PlayerCharacter::GetSingleton();

    if (player) {
        auto& runtime = player->GetPlayerRuntimeData();

        RE::BSSpinLockGuard guard{runtime.questTargetsLock};

        logger::trace("[QuestMarkers] runtime.questTargets contains {} quests", runtime.questTargets.size());

        for (const auto& [quest, targets] : runtime.questTargets) {
            logger::trace("[QuestMarkers] runtime target quest='{}' {:08X} targets={}",
                         quest && quest->GetName() ? quest->GetName() : "<unnamed>", quest ? quest->GetFormID() : 0,
                         targets ? targets->size() : 0);
        }
    }

    logger::trace(
        "[QuestMarkers] SUMMARY world='{}': activeObjectives={} withTargets={} resolvedRefs={} unresolvedRefs={} "
        "matchingObjectives={} hidden={}",
        targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>", activeObjectives, objectivesWithTargets,
        resolvedRefs, unresolvedRefs, matchingObjectives, hiddenCount);
}

std::pair<int, int> GetQuestStatsForWorld(RE::TESWorldSpace* targetWorld) {
    if (!targetWorld) {
        return {0, 0};
    }

    auto* dataHandler = RE::TESDataHandler::GetSingleton();

    if (!dataHandler) {
        return {-1, -1};
    }

    int ongoing = 0;
    int completed = 0;

    std::unordered_set<const RE::BGSQuestObjective*> hiddenSet;

    for (auto* objective : hiddenObjectives) {
        if (objective) {
            hiddenSet.insert(objective);
        }
    }

    const auto& quests = dataHandler->GetFormArray(RE::FormType::Quest);

    const auto uniqueActorRefs = BuildUniqueActorRefMap();

    for (auto* form : quests) {
        auto* quest = form ? form->As<RE::TESQuest>() : nullptr;

        if (!quest) {
            continue;
        }

        const bool questIsCompleted = quest->IsCompleted();

        bool belongsToWorld = false;
        bool hasOngoingObjective = false;

        for (auto* objective : quest->objectives) {
            if (!objective) {
                continue;
            }

            const bool displayed = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kDisplayed);

            const bool objectiveCompleted = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kCompleted);

            const bool objectiveFailed = objective->state.all(RE::QUEST_OBJECTIVE_STATE::kFailed);

            const bool candidate =
                questIsCompleted ? objectiveCompleted : displayed && !objectiveCompleted && !objectiveFailed;

            if (!candidate) {
                continue;
            }

            for (std::uint32_t i = 0; i < objective->numTargets; ++i) {
                auto* target = objective->targets[i];

                if (!target) {
                    continue;
                }

                auto* refAlias = FindQuestRefAlias(quest, target->alias);

                if (!refAlias) {
                    continue;
                }

                std::unordered_set<std::uint64_t> visited;

                if (!QuestAliasPointsToWorld(quest, refAlias, targetWorld, uniqueActorRefs, visited)) {
                    continue;
                }

                belongsToWorld = true;
                hasOngoingObjective = !questIsCompleted;
                break;
            }

            if (belongsToWorld) {
                break;
            }
        }

        if (questIsCompleted && !belongsToWorld) {
            for (auto* baseAlias : quest->aliases) {
                auto* refAlias = baseAlias ? skyrim_cast<RE::BGSRefAlias*>(baseAlias) : nullptr;

                if (!refAlias) {
                    continue;
                }

                std::unordered_set<std::uint64_t> visited;

                if (QuestAliasPointsToWorld(quest, refAlias, targetWorld, uniqueActorRefs, visited)) {
                    belongsToWorld = true;
                    break;
                }
            }
        }

        if (!belongsToWorld) {
            continue;
        }

        if (questIsCompleted) {
            ++completed;
        } else if (hasOngoingObjective) {
            ++ongoing;
        }
    }

    return {ongoing, completed};
}

void BackupCurrentWorld(RE::TESWorldSpace* currentWorld) {
    logger::trace("Backing up current world ({})", currentWorld->GetName());
    backup = currentWorld;
    backupWorld.worldMapData = currentWorld->worldMapData;
    backupWorld.parentWorld = currentWorld->parentWorld;
    backupWorld.parentUseFlags = currentWorld->parentUseFlags;
    backupWorld.mapMarkers = RE::PlayerCharacter::GetSingleton()->GetPlayerRuntimeData().currentMapMarkers;
    logger::trace("Backup complete");
}

void RestoreWorldspaceState(RE::TESWorldSpace* world, bool restoreClouds) {
    if (!world) {
        return;
    }

    world->worldMapData = backupWorld.worldMapData;
    world->parentWorld = backupWorld.parentWorld;
    world->parentUseFlags = backupWorld.parentUseFlags;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        player->GetPlayerRuntimeData().currentMapMarkers = backupWorld.mapMarkers;
    }

    /*for (auto* objective : hiddenObjectives) {
        if (objective) {
            objective->state.set(RE::QUEST_OBJECTIVE_STATE::kDisplayed);
        }
    }

    hiddenObjectives.clear();*/

    cloudSwapPending = false;
}

void DisplaySelectedWorld(RE::TESWorldSpace* currentWorld, RE::TESWorldSpace* targetWorld) {
    BackupCurrentWorld(currentWorld);
    auto player = RE::PlayerCharacter::GetSingleton();
    auto targetMarkers = GetMapMarkersInWorld(targetWorld);
    player->GetPlayerRuntimeData().currentMapMarkers = targetMarkers;
    logger::trace("[QuestMarkers] DisplaySelectedWorld current='{}' target='{}'",
                 currentWorld && currentWorld->GetName() ? currentWorld->GetName() : "<null>",
                 targetWorld && targetWorld->GetName() ? targetWorld->GetName() : "<null>");

    //UpdateQuestMarkers(targetWorld);

    /*currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseMapData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseLandData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseLODData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseWaterData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseClimateData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseImageSpaceData);
    currentWorld->parentUseFlags.set(RE::TESWorldSpace::ParentUseFlag::kUseSkyCell);

    
    g_pendingCarrier = currentWorld;
    g_pendingTarget = targetWorld;

    logger::info("[ParentWorldDebug] carrier={:X} parentWorld field={:X} target={:X}",
                 reinterpret_cast<std::uintptr_t>(currentWorld),
                 reinterpret_cast<std::uintptr_t>(&currentWorld->parentWorld),
                 reinterpret_cast<std::uintptr_t>(targetWorld));*/
}

bool IsCellInWorld(RE::BGSLocation* a_loc, RE::TESWorldSpace* a_world) {
    if (a_loc == a_world->location) {
        return true;
    } else if (a_loc->parentLoc) {
        return IsCellInWorld(a_loc->parentLoc, a_world);
    }
    return false;
}

bool IsMarkerInWorld(RE::TESWorldSpace* a_refWorld, RE::TESWorldSpace* a_targetWorld) {
    if (a_refWorld == a_targetWorld) {
        return true;
    }

    auto* realParent = a_refWorld == backup ? backupWorld.parentWorld : a_refWorld->parentWorld;

    if (realParent) {
        return IsMarkerInWorld(realParent, a_targetWorld);
    }

    return false;
}

void SetINIValue(const char* settingName, float value) {
    auto iniSetting = RE::INISettingCollection::GetSingleton()->GetSetting(settingName);
    if (iniSetting) {
        iniSetting->data.f = value;
        RE::INISettingCollection::GetSingleton()->WriteSetting(iniSetting);
    }
}

namespace {
    RE::TESWorldSpace* ResolveConfiguredWorldspace(RE::TESDataHandler* dataHandler, const nlohmann::json& worldData,
                                                   const std::string& pluginName) {
        if (!dataHandler || !worldData.contains("worldID") || !worldData["worldID"].is_string()) {
            return nullptr;
        }

        try {
            const auto worldIDString = worldData["worldID"].get<std::string>();

            const auto localFormID = static_cast<RE::FormID>(std::stoul(worldIDString, nullptr, 16));

            return dataHandler->LookupForm<RE::TESWorldSpace>(localFormID, pluginName);
        } catch (const std::exception& e) {
            logger::warn("Invalid worldID '{}' for world '{}': {}", worldData.value("worldID", "<missing>"),
                         worldData.value("worldName", "<unnamed>"), e.what());

            return nullptr;
        }
    }

    std::string NormalizeCloudModelPath(std::string path) {
        while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
            path.erase(path.begin());
        }

        std::replace(path.begin(), path.end(), '/', '\\');

        return path;
    }
}

void LoadWorldMaps() {
    try {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            logger::error(
                "Cannot load world maps: "
                "TESDataHandler is unavailable");

            return;
        }

        worldMapsData.clear();

        for (const auto& fileEntry : fs::directory_iterator(WORLD_MAPS_DIRECTORY)) {
            if (!fileEntry.is_regular_file() || fileEntry.path().extension() != ".json") {
                continue;
            }

            logger::info("Loading world map file: {}", fileEntry.path().string());

            std::ifstream file(fileEntry.path());

            if (!file.is_open()) {
                logger::warn("Failed to open JSON file: {}", fileEntry.path().string());

                continue;
            }

            try {
                nlohmann::json fileData;
                file >> fileData;

                if (!(fileData.contains("worldName") || fileData.contains("worldID")) ||
                    !fileData.contains("mapRegion") || !fileData.contains("plugin")) {
                    logger::warn(
                        "Skipping invalid JSON file: {} "
                        "(missing keys)",
                        fileEntry.path().string());

                    continue;
                }

                if (!fileData["plugin"].is_string()) {
                    logger::warn(
                        "Skipping world map file {}: "
                        "'plugin' must be a string",
                        fileEntry.path().filename().string());

                    continue;
                }

                if (fileData.contains("clouds") && !fileData["clouds"].is_string()) {
                    logger::warn(
                        "Skipping world map file {}: "
                        "'clouds' must be a string when present",
                        fileEntry.path().filename().string());

                    continue;
                }

                const auto pluginName = fileData["plugin"].get<std::string>();

                if (pluginName.empty()) {
                    logger::warn(
                        "Skipping world map file {}: "
                        "plugin name is empty",
                        fileEntry.path().filename().string());

                    continue;
                }

                const auto* plugin = dataHandler->LookupModByName(pluginName);

                if (!plugin) {
                    logger::info(
                        "Skipping world map file {}: "
                        "required plugin '{}' is not loaded",
                        fileEntry.path().filename().string(), pluginName);

                    continue;
                }

                if ((!plugin->IsLight() && plugin->compileIndex == 0xFF) ||
                    (plugin->IsLight() && plugin->smallFileCompileIndex == 0xFFFF)) {
                    logger::info("Skipping world map file {}: required plugin '{}' is inactive",
                                 fileEntry.path().filename().string(), pluginName);

                    continue;
                }

                if (!fileData.contains("priority")) {
                    fileData["priority"] = 9999;
                }

                if (!fileData.contains("description")) {
                    fileData["description"] = "";
                }

                if (!fileData.contains("symbol")) {
                    fileData["symbol"] = 0;
                }

                if (fileData.contains("clouds")) {
                    auto* worldspace = ResolveConfiguredWorldspace(dataHandler, fileData, pluginName);

                    if (!worldspace) {
                        logger::warn(
                            "Cannot apply cloud model for '{}': "
                            "worldspace could not be resolved. "
                            "A valid worldID is required.",
                            fileData.value("worldName", "<unnamed>"));
                    } else {
                        auto cloudModel = NormalizeCloudModelPath(fileData["clouds"].get<std::string>());

                        worldspace->SetModel(cloudModel.c_str());

                        logger::debug(
                            "Set cloud model for '{}' "
                            "(formID={:08X}) to '{}'",
                            worldspace->GetName(), worldspace->GetFormID(), cloudModel.empty() ? "<none>" : cloudModel);
                    }
                }

                if (fileData.contains("warmupWorld")) {
                    if (!fileData["warmupWorld"].is_number_integer()) {
                        logger::warn(
                            "World map file {} has invalid 'warmupWorld'; "
                            "expected integer 0 or 1. Warm-up disabled for this entry.",
                            fileEntry.path().filename().string());

                        fileData["warmupWorld"] = 0;
                    } else {
                        const auto warmupValue = fileData["warmupWorld"].get<std::int64_t>();

                        if (warmupValue != 0 && warmupValue != 1) {
                            logger::warn(
                                "World map file {} has invalid 'warmupWorld' value {}; "
                                "expected 0 or 1. Warm-up disabled for this entry.",
                                fileEntry.path().filename().string(), warmupValue);

                            fileData["warmupWorld"] = 0;
                        }
                    }
                }

                logger::debug(
                    "Successfully loaded world map '{}' "
                    "from plugin '{}'",
                    fileData.value("worldName", "<unnamed>"), pluginName);

                worldMapsData.push_back(std::move(fileData));

            } catch (const nlohmann::json::exception& e) {
                logger::error("JSON error in {}: {}", fileEntry.path().string(), e.what());

            } catch (const std::exception& e) {
                logger::error("Error processing JSON file {}: {}", fileEntry.path().string(), e.what());
            }
        }

        std::sort(worldMapsData.begin(), worldMapsData.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("priority", 9999) > b.value("priority", 9999);
        });

        logger::info("World map loading complete: {} valid entries", worldMapsData.size());

    } catch (const std::exception& e) {
        logger::error("Error iterating through directory {}: {}", WORLD_MAPS_DIRECTORY, e.what());
    }
}

void LoadTacticalData() {
    tacticalFactionsByID.clear();
    tacticalClimatesByID.clear();
    tacticalRegionsData = nlohmann::json::array();

    const auto loadDefinitions = [](const fs::path& directory, std::string_view type, auto& definitionsByID) {
        try {
            if (!fs::exists(directory)) {
                logger::warn("Tactical {} directory does not exist: {}", type, directory.string());

                return;
            }

            for (const auto& entry : fs::directory_iterator(directory)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                    continue;
                }

                const auto path = entry.path();

                try {
                    std::ifstream file(path);

                    if (!file.is_open()) {
                        logger::warn("Failed to open tactical {} JSON: {}", type, path.string());

                        continue;
                    }

                    nlohmann::json data;
                    file >> data;

                    if (!data.is_object() || !HasStringField(data, "id") || !HasStringField(data, "name") ||
                        !HasStringField(data, "colorMain")) {
                        logger::warn(
                            "Skipping invalid tactical {} file: {} "
                            "(required string fields: id, name, colorMain)",
                            type, path.string());

                        continue;
                    }

                    const auto id = data["id"].get<std::string>();
                    const auto name = data["name"].get<std::string>();
                    const auto color = data["colorMain"].get<std::string>();

                    if (id.empty() || name.empty() || color.empty()) {
                        logger::warn(
                            "Skipping tactical {} file with an empty "
                            "id, name, or colorMain: {}",
                            type, path.string());

                        continue;
                    }

                    if (definitionsByID.contains(id)) {
                        logger::warn("Skipping duplicate tactical {} ID '{}' from {}", type, id, path.string());

                        continue;
                    }

                    definitionsByID.emplace(id, std::move(data));

                    logger::trace("Successfully loaded tactical {} '{}'", type, id);
                } catch (const std::exception& e) {
                    logger::error("Error parsing tactical {} JSON {}: {}", type, path.string(), e.what());
                }
            }
        } catch (const std::exception& e) {
            logger::error("Error iterating through tactical {} directory {}: {}", type, directory.string(), e.what());
        }
    };

    loadDefinitions(TACTICAL_FACTIONS_DIRECTORY, "faction", tacticalFactionsByID);

    loadDefinitions(TACTICAL_CLIMATES_DIRECTORY, "climate", tacticalClimatesByID);

    try {
        if (!fs::exists(TACTICAL_REGIONS_DIRECTORY)) {
            logger::warn("Tactical regions directory does not exist: {}", TACTICAL_REGIONS_DIRECTORY);

            return;
        }

        std::unordered_set<std::string> loadedRegions;

        constexpr std::array<const char*, 4> seasons{"1", "2", "3", "4"};

        for (const auto& entry : fs::recursive_directory_iterator(TACTICAL_REGIONS_DIRECTORY)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            const auto path = entry.path();

            try {
                std::ifstream file(path);

                if (!file.is_open()) {
                    logger::warn("Failed to open tactical region JSON: {}", path.string());

                    continue;
                }

                nlohmann::json fileData;
                file >> fileData;

                if (!fileData.is_object()) {
                    logger::warn(
                        "Skipping invalid tactical region file: {} "
                        "(root is not an object)",
                        path.string());

                    continue;
                }

                const auto relativePath = fs::relative(path, TACTICAL_REGIONS_DIRECTORY);

                if (std::distance(relativePath.begin(), relativePath.end()) != 2) {
                    logger::warn(
                        "Skipping tactical region file with unexpected "
                        "folder depth: {}",
                        path.string());

                    continue;
                }

                const auto provinceName = relativePath.parent_path().filename().string();

                const auto regionID = relativePath.stem().string();

                if (provinceName.empty() || regionID.empty()) {
                    logger::warn(
                        "Skipping tactical region file with an empty "
                        "province or region ID: {}",
                        path.string());

                    continue;
                }

                const bool hasFaction = HasStringField(fileData, "factionId");

                const bool hasCrime = HasStringField(fileData, "crime");

                const bool hasSurvival = fileData.contains("survival");

                if (!hasFaction && !hasCrime && !hasSurvival) {
                    logger::warn(
                        "Skipping tactical region '{}:{}': must define at "
                        "least one of 'factionId', 'crime', or 'survival'",
                        provinceName, regionID);

                    continue;
                }

                const std::string regionKey = provinceName + ":" + regionID;

                if (!loadedRegions.emplace(regionKey).second) {
                    logger::warn("Skipping duplicate tactical region '{}' from {}", regionKey, path.string());

                    continue;
                }

                nlohmann::json regionData{{"id", regionID}, {"provinceName", provinceName}};

                // Faction
                if (hasFaction) {
                    const auto factionID = fileData["factionId"].get<std::string>();

                    if (!tacticalFactionsByID.contains(factionID)) {
                        logger::warn(
                            "Skipping tactical region '{}': "
                            "unknown faction ID '{}'",
                            regionKey, factionID);

                        loadedRegions.erase(regionKey);
                        continue;
                    }

                    regionData["factionId"] = factionID;
                }

                // Crime
                if (hasCrime) {
                    regionData["crime"] = fileData["crime"];
                }

                // Survival
                if (hasSurvival) {
                    const auto& survival = fileData["survival"];

                    if (!survival.is_object() || survival.size() != seasons.size()) {
                        logger::warn(
                            "Skipping tactical region '{}': 'survival' "
                            "must define exactly seasons 1-4",
                            regionKey);

                        loadedRegions.erase(regionKey);
                        continue;
                    }

                    bool validSurvival = true;

                    for (const auto* season : seasons) {
                        if (!survival.contains(season) || !survival[season].is_string()) {
                            logger::warn(
                                "Skipping tactical region '{}': missing "
                                "climate ID for season {}",
                                regionKey, season);

                            validSurvival = false;
                            break;
                        }

                        const auto climateID = survival[season].get<std::string>();

                        if (!tacticalClimatesByID.contains(climateID)) {
                            logger::warn(
                                "Skipping tactical region '{}': unknown "
                                "climate ID '{}' for season {}",
                                regionKey, climateID, season);

                            validSurvival = false;
                            break;
                        }
                    }

                    if (!validSurvival) {
                        loadedRegions.erase(regionKey);
                        continue;
                    }

                    regionData["survival"] = survival;
                }

                // Optional faction-switching fields
                for (const auto* field : {"loc", "keyword"}) {
                    if (HasStringField(fileData, field)) {
                        regionData[field] = fileData[field];
                    }
                }

                if (fileData.contains("switch") && fileData["switch"].is_object()) {
                    regionData["switch"] = fileData["switch"];
                }

                logger::trace("Successfully loaded tactical region: {}", regionData.dump());

                tacticalRegionsData.push_back(std::move(regionData));
            } catch (const std::exception& e) {
                logger::error("Error parsing tactical region JSON {}: {}", path.string(), e.what());
            }
        }
    } catch (const std::exception& e) {
        logger::error("Error iterating through tactical regions directory {}: {}", TACTICAL_REGIONS_DIRECTORY,
                      e.what());
    }

    logger::info("All tactical data loaded: {} factions, {} climates, {} regions", tacticalFactionsByID.size(),
                 tacticalClimatesByID.size(), tacticalRegionsData.size());
}

bool GetMapCenterOverride(RE::TESWorldSpace* world, RE::NiPoint3& position) {
    if (!world) {
        return false;
    }

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return false;
    }

    for (const auto& worldData : worldMapsData) {
        if (!worldData.contains("worldID") || !worldData.contains("plugin") || !worldData.contains("mapCenter")) {
            continue;
        }

        const auto& center = worldData["mapCenter"];

        if (!center.is_object() || !center.contains("x") || !center.contains("y") || !center["x"].is_number() ||
            !center["y"].is_number()) {
            logger::warn("[GetMapCenterOverride] Invalid mapCenter for '{}'", worldData.value("worldName", "?"));
            continue;
        }

        RE::FormID localFormID{};

        try {
            const auto formIDString = worldData["worldID"].get<std::string>();

            localFormID = static_cast<RE::FormID>(std::stoul(formIDString, nullptr, 16));
        } catch (const std::exception& e) {
            logger::warn("[GetMapCenterOverride] Invalid worldID for '{}': {}", worldData.value("worldName", "?"),
                         e.what());
            continue;
        }

        const auto plugin = worldData["plugin"].get<std::string>();

        auto* configuredWorld = dataHandler->LookupForm<RE::TESWorldSpace>(localFormID, plugin);

        if (configuredWorld != world) {
            continue;
        }

        position.x = center["x"].get<float>();
        position.y = center["y"].get<float>();

        logger::trace(
            "[GetMapCenterOverride] Using JSON center for '{}': "
            "({:.1f}, {:.1f})",
            world->GetName(), position.x, position.y);

        return true;
    }

    return false;
}
