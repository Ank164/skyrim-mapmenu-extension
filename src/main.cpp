#include <spdlog/sinks/basic_file_sink.h>

#include "Utility.h"
#include "EventProcessor.h"
#include "Hooks.h"
#include "Scaleform.h"
#include "MapExtension.h"
#include "MapExtensionHint.h"
#include "WorldspaceWarmup.h"

void SKSEMessageHandler(SKSE::MessagingInterface::Message* message) {
    auto eventProcessor = EventProcessor::GetSingleton();
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded: {
            RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(eventProcessor);

            RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESFastTravelEndEvent>(eventProcessor);

            Scaleform::MapExtension::Register();
            Scaleform::MapExtensionHint::Register();

            LoadWorldMaps();
            LoadTacticalData();

            if (enableWarmup) {
                g_warmupWorlds = GetWorldspacesMarkedForWarmup();
            } else {
                g_warmupWorlds.clear();
                logger::info("[WorldspaceWarmup] Disabled by configuration");
            }

            if (!g_warmupWorlds.empty()) {
                auto* world = g_warmupWorlds[0];

                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    taskInterface->AddTask([world]() {
                        if (world) {
                            logger::info("[WorldspaceWarmup] Warming index 0: '{}'", world->GetName());

                            WarmWorldspace(world);
                        }
                    });
                } else { 
                    logger::error("[WorldspaceWarmup] SKSE task interface unavailable");
                }
            }

            break;
        }
        case SKSE::MessagingInterface::kInputLoaded:
            RE::BSInputDeviceManager::GetSingleton()->AddEventSink<RE::InputEvent*>(eventProcessor);
            break;
        case SKSE::MessagingInterface::kPostLoadGame: {
            ResetRuntimeState();

            auto* player = RE::PlayerCharacter::GetSingleton();

            RE::TESWorldSpace* currentWorld = player ? player->GetWorldspace() : nullptr;

            // GetWorldspace() may be null when the save loads in an interior.
            if (!currentWorld && player) {
                currentWorld = player->GetPlayerRuntimeData().cachedWorldSpace;
            }

            RE::TESWorldSpace* worldToWarm = nullptr;
            std::size_t warmupIndex = 1;

            if (g_warmupWorlds.size() > 1) {
                worldToWarm = g_warmupWorlds[1];

                if (worldToWarm == currentWorld) {
                    logger::info(
                        "[WorldspaceWarmup] Skipping index 1 ('{}') "
                        "because it is the player's current world",
                        worldToWarm ? worldToWarm->GetName() : "<null>");

                    if (g_warmupWorlds.size() > 2) {
                        warmupIndex = 2;
                        worldToWarm = g_warmupWorlds[2];
                    } else {
                        worldToWarm = nullptr;
                    }
                }
            }

            if (worldToWarm) {
                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    taskInterface->AddTask([worldToWarm, warmupIndex]() {
                        logger::info("[WorldspaceWarmup] Warming index {}: '{}'", warmupIndex, worldToWarm->GetName());

                        WarmWorldspace(worldToWarm);
                    });
                } else {
                    logger::error("[WorldspaceWarmup] SKSE task interface unavailable");
                }
            } else {
                logger::info(
                    "[WorldspaceWarmup] No suitable post-load "
                    "worldspace to warm");
            }
            
            break;
        }
        case SKSE::MessagingInterface::kNewGame:
            ResetRuntimeState();
            break;
        default:
            break;
    }
}

extern "C" [[maybe_unused]] __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::AllocTrampoline(150);
    SetupLog();
    spdlog::set_level(spdlog::level::info);
    SKSE::GetMessagingInterface()->RegisterListener(SKSEMessageHandler);
    LoadDataFromINI();
    InstallHooks();
    logger::info("Successfully loaded MapMenuWorldspaceSelection.dll!");
    return true;
}