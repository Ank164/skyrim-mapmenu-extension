#include "RE/T/TESWorldSpace.h"
#include "RE/G/GFxEvent.h"
#include <xbyak/xbyak.h>
#include "Hooks.h"
#include "Utility.h"
#include "WorldspaceWarmup.h"
#include <Windows.h>
#include <MinHook.h>

using Accept_t = void(RE::MapMenu*, RE::FxDelegateHandler::CallbackProcessor*);
REL::Relocation<Accept_t> _Accept;
bool mapMenuInitialized = false;
bool otherWorld = false;
std::vector<RE::TESWorldSpace*> g_warmupWorlds;
RE::TESWorldSpace* targetWorld = nullptr;
RE::TESWorldSpace* pendingTargetWorld = nullptr;
RE::TESWorldSpace* g_journalCarrierWorld = nullptr;
using ResolveMapWorldSpace_t = RE::TESWorldSpace* (*)();
ResolveMapWorldSpace_t g_originalResolveMapWorldSpace = nullptr;

namespace {
    struct RawGFxKeyEvent {
        RE::GFxEvent event;
        std::uint32_t keyCode;
        std::uint8_t asciiCode;
        std::uint8_t pad09;
        std::uint16_t pad0A; 
        std::uint32_t wCharCode;
        std::uint8_t specialKeys;
        std::uint8_t keyboardIndex;
        std::uint16_t pad12;
    };

    static_assert(sizeof(RawGFxKeyEvent) == 0x14);

    bool g_mapMenuClosing = false;
    bool g_locationFinderOpen = false;
    bool g_gamepadFinderOpening = false;
    bool g_localMapOpen = false;

    void QueueLocalMapButtonState(bool a_visible);
}

struct CurrentLocationReturnHook1 {
    static bool thunk(RE::MapMenu* a_mapMenu) {
        if (otherWorld) {
            logger::trace("[CurrentLocation] Native return blocked (1)");
            return false;
        }

        return func(a_mapMenu);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct CurrentLocationReturnHook2 {
    static bool thunk(RE::MapMenu* a_mapMenu) {
        if (otherWorld) {
            logger::trace("[CurrentLocation] Native return blocked (2)");
            return false;
        }

        return func(a_mapMenu);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct LocalMapToggleHook {
    static std::uint64_t thunk(RE::MapMenu* a_mapMenu) {
        if (g_locationFinderOpen) {
            logger::trace(
                "[LocationFinder] Allowing Local Map input "
                "to close finder");

            const auto result = func(a_mapMenu);

            g_locationFinderOpen = false;
            g_gamepadFinderOpening = false;
            g_localMapOpen = false;

            if (otherWorld) {
                QueueLocalMapButtonState(false);
            }

            return result;
        }

        if (otherWorld) {
            logger::trace("[LocalMap] Native toggle blocked");
            return 0;
        }

        const auto result = func(a_mapMenu);

        g_localMapOpen = !g_localMapOpen;

        logger::trace(
            "[LocalMap] Player-world toggle completed; "
            "localMapOpen={}",
            g_localMapOpen);

        return result;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct InteriorLocalMapConditionHook {
    static bool thunk(void* a_state) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool result = func(a_state);

        if (result && (otherWorld || !player->GetParentCell()->IsInteriorCell())) {
            return false;
        }

        return result;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct LateLocalMapAutoOpenHook {
    static std::uint64_t thunk(RE::MapMenu* a_mapMenu, bool a_showLocalMap) {
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (a_showLocalMap &&
            (otherWorld || !player || !player->GetParentCell() || !player->GetParentCell()->IsInteriorCell())) {
            g_localMapOpen = false;

            logger::trace("[LocalMap] Native auto-open blocked");

            return 0;
        }

        const auto result = func(a_mapMenu, a_showLocalMap);

        g_localMapOpen = a_showLocalMap;

        logger::trace("[LocalMap] Native auto-open state: localMapOpen={}", g_localMapOpen);

        return result;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

namespace {
    RE::TESWorldSpace* g_cloudResolverWorld = nullptr;

    bool AreMapCloudsDisabledByCloudsFix() {
        if (!::GetModuleHandleA("MapMenuCloudsFix.dll")) {
            return false;
        }

        CSimpleIniA ini;
        ini.SetUnicode();

        constexpr const char* kCloudsFixINI = "Data/Map Menu Clouds Fix.ini";

        if (ini.LoadFile(kCloudsFixINI) < 0) {
            logger::warn(
                "[NativeMapClouds] MapMenuCloudsFix.dll detected, "
                "but failed to read '{}'",
                kCloudsFixINI);

            return false;
        }

        return !ini.GetBoolValue("MapMenu", "bEnableMapClouds", true);
    }

    namespace NativeMapClouds {
        using detach_t = void (*)();
        using release_t = void (*)();
        using load_t = void (*)(RE::TESWorldSpace*);
        using configure_t = void (*)(float); 

        static REL::Relocation<release_t> release{REL::ID(53149)};

        static REL::Relocation<configure_t> configure{REL::ID(53151)};

        static REL::Relocation<load_t> load{REL::ID(53157)};

        static REL::Relocation<std::uintptr_t> cloudCacheAddress{REL::ID(406687)};

        static REL::Relocation<detach_t> detach{REL::ID(53151), 0xA0};

        static bool cloudAttached = false;

        void ReleaseCache() {
            release();
        }

        void Unload() {
            auto** cache = reinterpret_cast<void**>(cloudCacheAddress.address());

            if (cache && *cache) {
                detach();
            }

            cloudAttached = false;
            ReleaseCache();
        }

        bool Reload(RE::TESWorldSpace* resolverWorld) {
            if (!resolverWorld) {
                logger::warn("[NativeMapClouds] Reload skipped: null world");
                return false;
            }

            auto** cache = reinterpret_cast<void**>(cloudCacheAddress.address());

            Unload();

            auto* effectiveWorld = resolverWorld->parentWorld ? resolverWorld->parentWorld : resolverWorld;

            constexpr RE::FormID kSkyrimWorldspaceID = 0x3C;

            const char* cloudModel = effectiveWorld->GetModel();

            if (effectiveWorld->GetFormID() != kSkyrimWorldspaceID && (!cloudModel || cloudModel[0] == '\0')) {
                cloudAttached = false;

                logger::info(
                    "[NativeMapClouds] '{}' has no cloud model; "
                    "using no map clouds",
                    effectiveWorld->GetName());

                return true;
            }

            load(resolverWorld);

            if (!cache || !*cache) {
                cloudAttached = false;

                logger::warn(
                    "[NativeMapClouds] Cloud model could not be loaded; "
                    "this world will use no clouds");

                return false;
            }

            configure(0.0f);
            cloudAttached = true;

            return true;
        }
    }

    bool ApplyCloudSetting(RE::TESWorldSpace* resolverWorld) {
        if (AreMapCloudsDisabledByCloudsFix()) {
            NativeMapClouds::Unload();

            logger::info(
                "[NativeMapClouds] Clouds disabled by Map Menu Clouds Fix; "
                "native cloud reload suppressed");

            return true;
        }

        return NativeMapClouds::Reload(resolverWorld);
    }

    bool HideBottomBarButton(RE::MapMenu* a_menu, std::uint32_t a_index) {
        if (!a_menu || !a_menu->uiMovie) {
            return false;
        }

        auto* movie = a_menu->uiMovie.get();

        const auto path = std::format("_root.bottomBar.buttonPanel.button{}", a_index);

        RE::GFxValue button;

        if (!movie->GetVariable(&button, path.c_str()) || !button.IsObject()) {
            return false;
        }

        RE::GFxValue trueValue;
        trueValue.SetBoolean(true);

        RE::GFxValue falseValue;
        falseValue.SetBoolean(false);

        button.SetMember("disabled", trueValue);
        button.SetMember("enabled", falseValue);
        button.SetMember("_visible", falseValue);
        button.SetMember("visible", falseValue);
        button.SetMember("mouseEnabled", falseValue);
        button.SetMember("focusEnabled", falseValue);
        button.SetMember("tabEnabled", falseValue);

        return true;
    }

    bool ShowBottomBarButton(RE::MapMenu* a_menu, std::uint32_t a_index) {
        if (!a_menu || !a_menu->uiMovie) {
            return false;
        }

        auto* movie = a_menu->uiMovie.get();

        const auto path = std::format("_root.bottomBar.buttonPanel.button{}", a_index);

        RE::GFxValue button;

        if (!movie->GetVariable(&button, path.c_str()) || !button.IsObject()) {
            return false;
        }

        RE::GFxValue trueValue;
        trueValue.SetBoolean(true);

        RE::GFxValue falseValue;
        falseValue.SetBoolean(false);

        button.SetMember("disabled", falseValue);
        button.SetMember("enabled", trueValue);
        button.SetMember("_visible", trueValue);
        button.SetMember("visible", trueValue);
        button.SetMember("mouseEnabled", trueValue);
        button.SetMember("focusEnabled", trueValue);
        button.SetMember("tabEnabled", trueValue);

        return true;
    }

    void RefreshBottomBar(RE::MapMenu* a_menu) {
        if (!a_menu || !a_menu->uiMovie) {
            return;
        }

        RE::GFxValue panel;

        if (!a_menu->uiMovie->GetVariable(&panel, "_root.bottomBar.buttonPanel") || !panel.IsObject()) {
            return;
        }

        RE::GFxValue instant;
        instant.SetBoolean(true);

        panel.Invoke("updateButtons", nullptr, &instant, 1);
    }

    void ApplyRemoteBottomBarState(RE::MapMenu* a_menu, bool a_showLocalMap) {
        if (!a_menu || !otherWorld) {
            return;
        }

        if (a_showLocalMap) {
            ShowBottomBarButton(a_menu, 0);
        } else {
            HideBottomBarButton(a_menu, 0);
        }

        HideBottomBarButton(a_menu, 3);
    }

    void QueueLocalMapButtonState(bool a_visible) {
        auto* tasks = SKSE::GetTaskInterface();

        if (!tasks) {
            return;
        }

        tasks->AddUITask([a_visible]() {
            if (!otherWorld || g_mapMenuClosing) {
                return;
            }

            auto* ui = RE::UI::GetSingleton();
            auto mapMenu = ui ? ui->GetMenu<RE::MapMenu>() : nullptr;

            if (!mapMenu) {
                return;
            }

            ApplyRemoteBottomBarState(mapMenu.get(), a_visible);

            RefreshBottomBar(mapMenu.get());
        });
    }

    void QueueRemoteBottomBarState() {
        auto* tasks = SKSE::GetTaskInterface();

        if (!tasks) {
            logger::warn("[MapControls] Task interface unavailable");
            return;
        }

        tasks->AddUITask([]() {
            if (!otherWorld || g_mapMenuClosing) {
                return;
            }

            auto* ui = RE::UI::GetSingleton();
            auto mapMenu = ui ? ui->GetMenu<RE::MapMenu>() : nullptr;

            if (!mapMenu) {
                return;
            }

            ApplyRemoteBottomBarState(mapMenu.get(), false);

            RefreshBottomBar(mapMenu.get());
        });
    }

    bool IsBottomBarButtonVisible(RE::MapMenu* a_menu, std::uint32_t a_index) {
        if (!a_menu || !a_menu->uiMovie) {
            return false;
        }

        const auto path = std::format("_root.bottomBar.buttonPanel.button{}", a_index);

        RE::GFxValue button;

        if (!a_menu->uiMovie->GetVariable(&button, path.c_str()) || !button.IsObject()) {
            return false;
        }

        RE::GFxValue visible;

        if (button.GetMember("_visible", &visible) && visible.IsBool()) {
            return visible.GetBool();
        }

        return false;
    }

    int QueryLocalMapState(RE::MapMenu* a_menu) {
        if (!a_menu || !a_menu->uiMovie) {
            return -1;
        }

        RE::GFxValue state;

        if (!a_menu->uiMovie->GetVariable(&state, "_root.localMapFader.MapClip._state") || !state.IsNumber()) {
            return -1;
        }

        return static_cast<int>(state.GetNumber());
    }
}

void OpenLocationFinderFromGamepad() {
    g_gamepadFinderOpening = true;

    logger::trace("[LocationFinder] Gamepad opening armed");
}

RE::TESWorldSpace* ResolveMapWorldSpaceHook() {
    auto* vanillaWorld = g_originalResolveMapWorldSpace ? g_originalResolveMapWorldSpace() : nullptr;

    if (!g_mapWorldOverrideActive || !targetWorld) {
        return vanillaWorld;
    }

    logger::trace("[WorldResolver] OVERRIDE vanilla='{}' {:08X} -> target='{}' {:08X}",
                  vanillaWorld && vanillaWorld->GetName() ? vanillaWorld->GetName() : "<null>",
                  vanillaWorld ? vanillaWorld->GetFormID() : 0,
                  targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>", targetWorld->GetFormID());

    return targetWorld;
}

bool InstallWorldspaceResolverHook() {
    REL::Relocation<std::uintptr_t> resolver{REL::RelocationID(52260, 53150)};

    const auto status =
        MH_CreateHook(reinterpret_cast<void*>(resolver.address()), reinterpret_cast<void*>(&ResolveMapWorldSpaceHook),
                      reinterpret_cast<void**>(&g_originalResolveMapWorldSpace));

    if (status != MH_OK) {
        logger::error("[WorldResolver] MH_CreateHook failed: {}", MH_StatusToString(status));

        return false;
    }

    logger::info("[WorldResolver] Created resolver hook at {:X}", resolver.address());

    return true;
}

void Hook_Accept(RE::MapMenu* menu, RE::FxDelegateHandler::CallbackProcessor* processor) {
    const bool initializing = !mapMenuInitialized;

    if (!initializing) {
        _Accept(menu, processor);
        return;
    }

    g_locationFinderOpen = false;

    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!player || !menu) {
        mapMenuInitialized = true;
        _Accept(menu, processor);
        return;
    }

    RE::TESWorldSpace* carrierWorld = nullptr;

    if (g_journalCarrierWorld) {
        carrierWorld = g_journalCarrierWorld;

        logger::trace("[JournalMap] Using saved carrier '{}' for Journal map open",
                     carrierWorld->GetName() ? carrierWorld->GetName() : "<unnamed>");
    } else {
        carrierWorld = player->GetWorldspace();
    }

    if (!carrierWorld) {
        carrierWorld = menu->GetRuntimeData2()->worldSpace;
    }

    if (!carrierWorld) {
        carrierWorld = player->GetPlayerRuntimeData().cachedWorldSpace;
    }

    if (!carrierWorld) {
        logger::warn(
            "[Hook_Accept] No map carrier worldspace found; "
            "using Skyrim");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        if (dataHandler) {
            carrierWorld = dataHandler->LookupForm<RE::TESWorldSpace>(0x3C, "Skyrim.esm");
        }
    }

    if (!carrierWorld) {
        logger::error("[Hook_Accept] No usable worldspace found");

        mapMenuInitialized = true;
        _Accept(menu, processor);
        return;
    }

    if (carrierWorld->parentWorld) {
        carrierWorld = carrierWorld->parentWorld;
    }

    if (!targetWorld) {
        targetWorld = carrierWorld;
    }

    if (carrierWorld == targetWorld) {
        otherWorld = false;
    } else {
        DisplaySelectedWorld(carrierWorld, targetWorld);

        otherWorld = true;
    }

    g_cloudResolverWorld = nullptr;
    cloudSwapPending = false;

    cloudSwapPending = true;
    g_cloudResolverWorld = targetWorld;

    /**if (otherWorld && g_pendingCarrier && g_pendingTarget) {
        g_pendingCarrier->parentWorld = g_pendingTarget;

        logger::info("[ParentWorldDebug] carrier={:X} parentWorld field={:X} target={:X}",
                     reinterpret_cast<std::uintptr_t>(g_pendingCarrier),
                     reinterpret_cast<std::uintptr_t>(&g_pendingCarrier->parentWorld),
                     reinterpret_cast<std::uintptr_t>(g_pendingTarget));

        logger::info("[ParentWorldDebug] parent assigned immediately before native Accept");
    }*/

    mapMenuInitialized = true;
    _Accept(menu, processor);

    g_journalCarrierWorld = nullptr;
}

struct MapMenuProcessMessageHook {
    static RE::UI_MESSAGE_RESULTS thunk(RE::MapMenu* a_menu, RE::UIMessage& a_message) {
        const auto messageType = a_message.type.get();

        bool keyboardFinderOpening = false;

        if (messageType == RE::UI_MESSAGE_TYPE::kScaleformEvent && a_message.data) {
            auto* scaleformData = static_cast<RE::BSUIScaleformData*>(a_message.data);

            auto* event = scaleformData->scaleformEvent;

            if (event && event->type == RE::GFxEvent::EventType::kKeyDown) {
                const auto* keyEvent = reinterpret_cast<const RawGFxKeyEvent*>(event);

                if (keyEvent->keyCode == 70) {
                    keyboardFinderOpening = true;
                }
            }
        }

        if (messageType == RE::UI_MESSAGE_TYPE::kHide || messageType == RE::UI_MESSAGE_TYPE::kForceHide) {
            logger::trace("[ProcessMessage] MapMenu hide requested");

            g_mapMenuClosing = true;

            cloudSwapPending = false;
            g_cloudResolverWorld = nullptr;

            const auto result = func(a_menu, a_message);

            NativeMapClouds::Unload();

            g_locationFinderOpen = false;
            g_gamepadFinderOpening = false;
            g_localMapOpen = false;

            g_mapMenuClosing = false;

            return result;
        }

        if (messageType == RE::UI_MESSAGE_TYPE::kShow) {
            g_locationFinderOpen = false;
            g_gamepadFinderOpening = false;
            g_localMapOpen = false;
        }

        const auto result = func(a_menu, a_message);

        if (messageType == RE::UI_MESSAGE_TYPE::kShow) {

            if (otherWorld) {
                QueueRemoteBottomBarState();
            }

            logger::trace("[MapMenu] Show complete: localMapOpen={}", g_localMapOpen);

            return result;
        }

        if (messageType != RE::UI_MESSAGE_TYPE::kScaleformEvent) {
            return result;
        }

        const bool finderOpening = keyboardFinderOpening || g_gamepadFinderOpening;

        if (finderOpening) {
            g_locationFinderOpen = true;
            g_gamepadFinderOpening = false;
            g_localMapOpen = false;

            if (otherWorld) {
                QueueLocalMapButtonState(true);
            }

            logger::trace("[LocationFinder] Opening event processed");

            return result;
        }

        if (!g_locationFinderOpen) {
            return result;
        }

        if (!otherWorld) {
            return result;
        }

        const bool vanillaBarWasRestored = IsBottomBarButtonVisible(a_menu, 3);

        if (vanillaBarWasRestored) {
            g_locationFinderOpen = false;
            g_gamepadFinderOpening = false;

            ApplyRemoteBottomBarState(a_menu, false);

            RefreshBottomBar(a_menu);

            logger::trace("[LocationFinder] Vanilla bottom bar restored");
        } else {
            ApplyRemoteBottomBarState(a_menu, true);
        }

        return result;
    }

    static inline REL::Relocation<decltype(thunk)> func;

    static void Install() {
        REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_MapMenu[0]};

        func = vtable.write_vfunc(0x04, thunk);
    }
};

//Hides player map marker, credit goes to SkyHorizon3 (https://github.com/SkyHorizon3/SSE-No-Google-Maps-Skyrim)
struct PlayerMarkerHook {
    static bool thunk(RE::BSTArray<RE::MapMenuMarker>* playerMarker, RE::NiPoint3* playerMarkerPos) {
        if (otherWorld) return false;

        return func(playerMarker, playerMarkerPos);
    };

    static bool thunkVR(RE::BSTArray<RE::MapMenuMarker>* playerMarker, RE::NiPoint3* playerMarkerPos,
                        RE::NiPoint3* unk) {
        if (otherWorld) return false;

        return funcVR(playerMarker, playerMarkerPos, unk);
    };

    static inline REL::Relocation<decltype(thunk)> func;
    static inline REL::Relocation<decltype(thunkVR)> funcVR;

    static void Install() {
        REL::Relocation<std::uintptr_t> markerHook{REL::VariantID(52221, 53108, 0x9184C0),
                                                   REL::Relocate(0x121, 0x121, 0x124)};

        if (REL::Module::IsVR())
            funcVR = SKSE::GetTrampoline().write_call<5>(markerHook.address(), thunkVR);
        else
            func = SKSE::GetTrampoline().write_call<5>(markerHook.address(), thunk);
    }
};

namespace QuestMarkerRouteOverride {
    thread_local bool processingRemoteQuestRoutes = false;

    using AppendQuestMarkers_t = void (*)(RE::BSTArray<RE::MapMenuMarker>*, void*, std::uint32_t);

    using ResolveRoutedMarkerHandle_t = RE::RefHandle* (*)(RE::RefHandle*, RE::RefHandle*, RE::TeleportPath*,
                                                           std::uint32_t, bool);

    using RouteEntriesShareRootWorldspace_t = bool (*)(RE::TeleportPath*);

    AppendQuestMarkers_t originalAppendQuestMarkers = nullptr;
    ResolveRoutedMarkerHandle_t originalResolveRoutedMarkerHandle = nullptr;
    RouteEntriesShareRootWorldspace_t originalRouteEntriesShareRootWorldspace = nullptr;

    struct ScopedRouteOverride {
        bool previous;

        ScopedRouteOverride() : previous(processingRemoteQuestRoutes) { processingRemoteQuestRoutes = true; }

        ~ScopedRouteOverride() { processingRemoteQuestRoutes = previous; }
    };

    std::optional<RE::TeleportPath> TrimRouteToTargetWorld(const RE::TeleportPath* route) {
        if (!route || !otherWorld || !targetWorld) {
            return std::nullopt;
        }

        std::optional<std::size_t> targetIndex;

        for (std::size_t i = 0; i < route->spaces.size(); ++i) {
            const auto& space = route->spaces[i];

            if (space.isWorldspace && space.worldspace && IsMarkerInWorld(space.worldspace, targetWorld)) {
                targetIndex = i;
                break;
            }
        }

        if (!targetIndex) {
            logger::trace("[QuestRoute] Reject route: never enters '{}'",
                          targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>");

            return std::nullopt;
        }

        RE::TeleportPath trimmed{};

        for (std::size_t i = *targetIndex; i < route->spaces.size(); ++i) {
            trimmed.spaces.push_back(route->spaces[i]);
        }

        for (std::size_t i = *targetIndex; i < route->teleportRefs.size(); ++i) {
            trimmed.teleportRefs.push_back(route->teleportRefs[i]);
        }

        trimmed.start = route->start;
        trimmed.end = route->end;

        logger::trace("[QuestRoute] Trimmed for '{}': entries {} -> {}, refs {} -> {}",
                      targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>", route->spaces.size(),
                      trimmed.spaces.size(), route->teleportRefs.size(), trimmed.teleportRefs.size());

        return trimmed;
    }

    void AppendQuestMarkersHook(RE::BSTArray<RE::MapMenuMarker>* markers, void* objectives, std::uint32_t mapMode) {
        logger::trace("[QuestRoute] AppendQuestMarkers called otherWorld={} target='{}' mapMode={}", otherWorld,
                     targetWorld && targetWorld->GetName() ? targetWorld->GetName() : "<none>", mapMode);

        if (!otherWorld || !targetWorld || mapMode != 0) {
            originalAppendQuestMarkers(markers, objectives, mapMode);
            return;
        }

        ScopedRouteOverride guard;

        originalAppendQuestMarkers(markers, objectives, mapMode);
    }

    RE::RefHandle* ResolveRoutedMarkerHandleHook(RE::RefHandle* result, RE::RefHandle* originalHandle,
                                                 RE::TeleportPath* route, std::uint32_t mapMode, bool validate) {
        if (processingRemoteQuestRoutes) {
            logger::trace("[QuestRoute] ResolveRoutedMarkerHandle route={} mapMode={}", static_cast<void*>(route),
                         mapMode);
        }

        if (!processingRemoteQuestRoutes) {
            return originalResolveRoutedMarkerHandle(result, originalHandle, route, mapMode, validate);
        }

        if (!result) {
            return nullptr;
        }

        auto trimmed = TrimRouteToTargetWorld(route);

        if (!trimmed) {
            *result = 0;
            return result;
        }

        return originalResolveRoutedMarkerHandle(result, originalHandle, std::addressof(*trimmed), mapMode, validate);
    }

    bool RouteEntriesShareRootWorldspaceHook(RE::TeleportPath* route) {
        if (processingRemoteQuestRoutes) {
            logger::trace("[QuestRoute] RouteEntriesShareRootWorldspace route={}", static_cast<void*>(route));
        }

        if (!processingRemoteQuestRoutes) {
            return originalRouteEntriesShareRootWorldspace(route);
        }

        auto trimmed = TrimRouteToTargetWorld(route);

        return trimmed && originalRouteEntriesShareRootWorldspace(std::addressof(*trimmed));
    }

    bool Install() {
        const auto append = REL::ID(53073).address();
        const auto resolve = REL::ID(53075).address();
        const auto validate = REL::ID(53085).address();

        if (MH_CreateHook(reinterpret_cast<void*>(append), reinterpret_cast<void*>(AppendQuestMarkersHook),
                          reinterpret_cast<void**>(&originalAppendQuestMarkers)) != MH_OK) {
            logger::error("[QuestRoute] Failed to hook AppendQuestMarkers");
            return false;
        }

        if (MH_CreateHook(reinterpret_cast<void*>(resolve), reinterpret_cast<void*>(ResolveRoutedMarkerHandleHook),
                          reinterpret_cast<void**>(&originalResolveRoutedMarkerHandle)) != MH_OK) {
            logger::error("[QuestRoute] Failed to hook ResolveRoutedMarkerHandle");
            return false;
        }

        if (MH_CreateHook(reinterpret_cast<void*>(validate),
                          reinterpret_cast<void*>(RouteEntriesShareRootWorldspaceHook),
                          reinterpret_cast<void**>(&originalRouteEntriesShareRootWorldspace)) != MH_OK) {
            logger::error("[QuestRoute] Failed to hook RouteEntriesShareRootWorldspace");
            return false;
        }

        logger::trace("[QuestRoute] Append + Resolve + Root hooks created");
        return true;
    }
}

namespace JournalShowOnMapOverride {
    using BuildQuestTargetHandle_t = int* (*)(int*, int*, std::int64_t, int, char);

    using ShowTargetOnMap_t = std::int64_t (*)(std::int64_t);

    using ValidateQuestTarget_t = bool (*)(std::int64_t);

    BuildQuestTargetHandle_t originalBuildQuestTargetHandle = nullptr;

    ShowTargetOnMap_t originalShowTargetOnMap = nullptr;
    ValidateQuestTarget_t originalValidateQuestTarget = nullptr;

    thread_local std::int64_t remoteJournalRoute = 0;
    thread_local std::int64_t blockedJournalRoute = 0;

    RE::TESWorldSpace* FindConfiguredWorldForReference(RE::TESObjectREFR* ref) {
        if (!ref) {
            return nullptr;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            return nullptr;
        }

        for (const auto& worldData : worldMapsData) {
            if (!worldData.contains("worldID") || !worldData["worldID"].is_string() || !worldData.contains("plugin") ||
                !worldData["plugin"].is_string()) {
                continue;
            }

            try {
                const auto localFormID =
                    static_cast<RE::FormID>(std::stoul(worldData["worldID"].get<std::string>(), nullptr, 16));

                const auto plugin = worldData["plugin"].get<std::string>();

                auto* world = dataHandler->LookupForm<RE::TESWorldSpace>(localFormID, plugin);

                if (!world) {
                    continue;
                }

                if (IsQuestReferenceInWorld(ref, world)) {
                    return world;
                }

            } catch (...) {
                continue;
            }
        }

        return nullptr;
    }

    int* BuildQuestTargetHandleHook(int* result, int* originalHandle, std::int64_t routeData, int directTarget,
                                    char validate) {
        remoteJournalRoute = 0;
        blockedJournalRoute = 0;

        auto* returned = originalBuildQuestTargetHandle(result, originalHandle, routeData, directTarget, validate);

        if (!result || !originalHandle || !*originalHandle) {
            return returned;
        }

        const auto* handle = reinterpret_cast<const RE::ObjectRefHandle*>(originalHandle);

        auto ref = handle->get();

        if (!ref) {
            return returned;
        }

        auto* configuredWorld = FindConfiguredWorldForReference(ref.get());

        if (!configuredWorld) {
            return returned;
        }

        if (!targetWorld || !IsQuestReferenceInWorld(ref.get(), targetWorld)) {
            blockedJournalRoute = routeData;

            logger::trace("[JournalMap] Blocking target {:08X}: objective='{}', displayed='{}'", ref->GetFormID(),
                          configuredWorld->GetName() ? configuredWorld->GetName() : "<unnamed>",
                          targetWorld && targetWorld->GetName() ? targetWorld->GetName() : "<none>");

            return returned;
        }

        remoteJournalRoute = routeData;

        logger::trace(
            "[JournalMap] Allowing target {:08X} for displayed world '{}' "
            "(originalHandle={:08X}, routedHandle={:08X})",
            ref->GetFormID(), targetWorld->GetName() ? targetWorld->GetName() : "<unnamed>",
            static_cast<std::uint32_t>(*originalHandle), static_cast<std::uint32_t>(*result));

        return returned;
    }

    bool ValidateQuestTargetHook(std::int64_t routeData) {
        const bool vanilla = originalValidateQuestTarget(routeData);

        const bool blocked = blockedJournalRoute != 0 && blockedJournalRoute == routeData;

        const bool remote = remoteJournalRoute != 0 && remoteJournalRoute == routeData;

        logger::trace(
            "[JournalMap] Validate target route={:X} "
            "vanilla={} remote={} blocked={}",
            static_cast<std::uintptr_t>(routeData), vanilla, remote, blocked);

        if (blocked) {
            blockedJournalRoute = 0;
            return false;
        }

        if (remote) {
            remoteJournalRoute = 0;
            return true;
        }

        return vanilla;
    }

    std::int64_t ShowTargetOnMapHook(std::int64_t callbackParams) {
        if (callbackParams) {
            const auto args = *reinterpret_cast<std::uintptr_t*>(callbackParams + 0x28);

            if (args) {
                const auto questTargetID =
                    static_cast<RE::FormID>(static_cast<std::int32_t>(*reinterpret_cast<double*>(args + 0x10)));

                if (auto* form = RE::TESForm::LookupByID(questTargetID)) {
                    if (auto* ref = form->As<RE::TESObjectREFR>()) {
                        if (auto* world = FindConfiguredWorldForReference(ref)) {
                            auto* player = RE::PlayerCharacter::GetSingleton();

                            g_journalCarrierWorld = player ? player->GetWorldspace() : nullptr;

                            if (!g_journalCarrierWorld && player) {
                                g_journalCarrierWorld = player->GetPlayerRuntimeData().cachedWorldSpace;
                            }

                            if (g_journalCarrierWorld && g_journalCarrierWorld->parentWorld) {
                                g_journalCarrierWorld = g_journalCarrierWorld->parentWorld;
                            }

                            targetWorld = world;

                            logger::trace("[JournalMap] ShowTargetOnMap {:08X}: carrier='{}' target='{}'",
                                          questTargetID,
                                         g_journalCarrierWorld && g_journalCarrierWorld->GetName()
                                             ? g_journalCarrierWorld->GetName()
                                             : "<null>",
                                         world->GetName() ? world->GetName() : "<unnamed>");
                        }
                    }
                }
            }
        }

        return originalShowTargetOnMap(callbackParams);
    }

    bool Install() {
        static REL::Relocation<std::uintptr_t> journalTargetHelper{REL::ID(53179)};

        const auto buildTargetCall = journalTargetHelper.address() + 0x125;

        originalBuildQuestTargetHandle = reinterpret_cast<BuildQuestTargetHandle_t>(
            SKSE::GetTrampoline().write_call<5>(buildTargetCall, BuildQuestTargetHandleHook));

        logger::trace("[JournalMap] Quest target builder call hooked at {:X}", buildTargetCall);

        static REL::Relocation<std::uintptr_t> showTarget{REL::ID(53178)};

        const auto status =
            MH_CreateHook(reinterpret_cast<void*>(showTarget.address()), reinterpret_cast<void*>(&ShowTargetOnMapHook),
                          reinterpret_cast<void**>(&originalShowTargetOnMap));

        if (status != MH_OK) {
            logger::error("[JournalMap] Failed to hook ShowTargetOnMap: {}", MH_StatusToString(status));
            return false;
        }

        const auto validateTargetCall = journalTargetHelper.address() + 0x153;

        originalValidateQuestTarget = reinterpret_cast<ValidateQuestTarget_t>(
            SKSE::GetTrampoline().write_call<5>(validateTargetCall, ValidateQuestTargetHook));

        logger::trace("[JournalMap] Quest target validation call hooked at {:X}", validateTargetCall);

        logger::trace("[JournalMap] Show-on-map hooks created");

        return true;
    }
}

bool NormalizeMapBeforeExtensionOpen() {
    auto* ui = RE::UI::GetSingleton();
    auto mapMenu = ui ? ui->GetMenu<RE::MapMenu>() : nullptr;

    if (!mapMenu) {
        g_locationFinderOpen = false;
        g_gamepadFinderOpening = false;
        g_localMapOpen = false;

        logger::warn(
            "[MapExtension] Could not query map state: "
            "MapMenu unavailable");

        return false;
    }

    const int localMapState = QueryLocalMapState(mapMenu.get());

    logger::trace(
        "[MapExtension] Actual native map state before opening: "
        "{} (tracked finderOpen={}, localMapOpen={})",
        localMapState, g_locationFinderOpen, g_localMapOpen);

    // 0 = World Map
    if (localMapState == 0) {
        g_locationFinderOpen = false;
        g_gamepadFinderOpening = false;
        g_localMapOpen = false;

        return false;
    }

    // 1 = Local Map
    if (localMapState == 1) {
        logger::trace("[MapExtension] Returning actual Local Map to World Map");

        LocalMapToggleHook::func(mapMenu.get());

        g_locationFinderOpen = false;
        g_gamepadFinderOpening = false;
        g_localMapOpen = false;

        return true;
    }

    // 2 = Location Finder
    if (localMapState == 2) {
        logger::trace("[MapExtension] Closing actual Location Finder");

        LocalMapToggleHook::func(mapMenu.get());

        const int stateAfterFinderClose = QueryLocalMapState(mapMenu.get());

        logger::trace("[MapExtension] State after Finder close: {}", stateAfterFinderClose);

        if (stateAfterFinderClose == 1) {
            logger::trace(
                "[MapExtension] Finder left Local Map active; "
                "returning it to World Map");

            LocalMapToggleHook::func(mapMenu.get());
        }

        g_locationFinderOpen = false;
        g_gamepadFinderOpening = false;
        g_localMapOpen = false;

        return true;
    }

    logger::warn(
        "[MapExtension] Unknown native Local Map state {}; "
        "leaving it untouched",
        localMapState);

    return false;
}

void ResetRuntimeState() {
    g_mapMenuClosing = false;
    g_locationFinderOpen = false;
    g_gamepadFinderOpening = false;
    g_localMapOpen = false;
    cloudSwapPending = false;
    g_cloudResolverWorld = nullptr;
    pendingWorldSwitch = false;
    pendingMapReopen = false;
    mapMenuInitialized = false;
    otherWorld = false;
    targetWorld = nullptr;
    pendingTargetWorld = nullptr;
    g_journalCarrierWorld = nullptr;
    backup = nullptr;
    backupWorld = {};
    g_mapWorldOverrideActive = false;
    hiddenObjectives.clear();

    logger::info("[RuntimeReset] Map runtime state reset");
}

// Centers camera on map, credit goes to SkyHorizon3 (https://github.com/SkyHorizon3/SSE-No-Google-Maps-Skyrim)
inline bool getMaxHeightAt(RE::TESWorldSpace* worldSpace, const RE::NiPoint3& point, float& outHeight) {
    using func_t = decltype(&getMaxHeightAt);
    static REL::Relocation<func_t> func{RELOCATION_ID(20103, 20551)};
    return func(worldSpace, point, outHeight);
}

struct SetMapCameraRootHook {
    static void thunk(RE::MapCamera* camera, RE::NiNode* root, const RE::NiPoint3& mapPos) {
        if (g_mapMenuClosing) {
            func(camera, root, mapPos);

            return;
        }

        if (!otherWorld || !targetWorld) {
            func(camera, root, mapPos);

            if (cloudSwapPending) {
                auto* resolverWorld = g_cloudResolverWorld;

                cloudSwapPending = false;
                g_cloudResolverWorld = nullptr;

                if (!resolverWorld) {
                    return;
                }

                ApplyCloudSetting(resolverWorld);
            }

            return;
        }

        auto* mapWorld = targetWorld;

        const auto& mapData = mapWorld->worldMapData;

        RE::NiPoint3 pos{};
        pos.z = mapPos.z;

        const bool hasCenterOverride = GetMapCenterOverride(mapWorld, pos);

        if (!hasCenterOverride) {
            const float seX = static_cast<float>(mapData.seCellX) * 4096.0f;

            const float seY = static_cast<float>(mapData.seCellY) * 4096.0f;

            const float nwX = static_cast<float>(mapData.nwCellX) * 4096.0f;

            const float nwY = static_cast<float>(mapData.nwCellY) * 4096.0f;

            pos.x = (seX + nwX) * 0.5f;

            pos.y = (seY + nwY) * 0.5f;
        }

        static const bool fwmfFound = ::GetModuleHandleA("FlatMapMarkersSSE.dll") != nullptr;

        if (fwmfFound) {
            pos.z = 180000.0f;
        } else {
            float maxHeight{};

            const bool foundHeight = getMaxHeightAt(mapWorld, pos, maxHeight);

            if (foundHeight) {
                pos.z = maxHeight;
            }
        }

        logger::trace(
            "[MapCameraHeight] Final map camera position for '{}': "
            "({:.2f}, {:.2f}, {:.2f})",
            mapWorld->GetName() ? mapWorld->GetName() : "<unnamed>", pos.x, pos.y, pos.z);

        func(camera, root, pos);

        if (!cloudSwapPending) {
            return;
        }

        auto* resolverWorld = g_cloudResolverWorld;

        cloudSwapPending = false;
        g_cloudResolverWorld = nullptr;

        if (!resolverWorld) {
            logger::error("[NativeMapClouds] Deferred native swap has no resolver");

            return;
        }

        ApplyCloudSetting(resolverWorld);

        auto* effectiveWorld = resolverWorld->parentWorld ? resolverWorld->parentWorld : resolverWorld;
    }

    static inline REL::Relocation<decltype(thunk)> func;

    static void Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_MapCamera[0]};

        func = vtbl.write_vfunc(0x3, thunk);
    }
};

void InstallHooks() {
    const auto mhStatus = MH_Initialize();

    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        logger::error("[MinHook] Initialization failed: {}", MH_StatusToString(mhStatus));

        return;
    }

    PlayerMarkerHook::Install();
    SetMapCameraRootHook::Install();
    MapMenuProcessMessageHook::Install();

    if (!QuestMarkerRouteOverride::Install()) {
        logger::error("[QuestRoute] Failed to install quest marker route hooks");

        return;
    }

    if (!JournalShowOnMapOverride::Install()) {
        logger::error("[JournalMap] Failed to install journal map hooks");

        return;
    }

    if (!InstallWorldspaceResolverHook()) {
        logger::error("[WorldResolver] Failed to install resolver hook");

        return;
    }

    REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_MapMenu[0]};

    _Accept = vtbl.write_vfunc(0x01, Hook_Accept);

    REL::Relocation<std::uintptr_t> currentLocationCall1{REL::VariantID(52215, 53102, 0x916D10),
                                                         REL::Relocate(0x55D, 0x58F, 0xA82)};

    CurrentLocationReturnHook1::func =
        SKSE::GetTrampoline().write_call<5>(currentLocationCall1.address(), CurrentLocationReturnHook1::thunk);

    REL::Relocation<std::uintptr_t> currentLocationCall2{REL::VariantID(52215, 53102, 0x916D10),
                                                         REL::Relocate(0x63B, 0x66D, 0xC61)};

    CurrentLocationReturnHook2::func =
        SKSE::GetTrampoline().write_call<5>(currentLocationCall2.address(), CurrentLocationReturnHook2::thunk);

    REL::Relocation<std::uintptr_t> localMapToggleCall{REL::ID(53102), 0x5A5};

    LocalMapToggleHook::func =
        SKSE::GetTrampoline().write_call<5>(localMapToggleCall.address(), LocalMapToggleHook::thunk);

    REL::Relocation<std::uintptr_t> interiorLocalMapConditionCall{REL::ID(53119), 0xA9};

    InteriorLocalMapConditionHook::func = SKSE::GetTrampoline().write_call<5>(interiorLocalMapConditionCall.address(),
                                                                              InteriorLocalMapConditionHook::thunk);

    REL::Relocation<std::uintptr_t> lateLocalMapAutoOpenCall{REL::ID(53119), 0x11D};

    LateLocalMapAutoOpenHook::func =
        SKSE::GetTrampoline().write_call<5>(lateLocalMapAutoOpenCall.address(), LateLocalMapAutoOpenHook::thunk);

    const auto enableStatus = MH_EnableHook(MH_ALL_HOOKS);

    if (enableStatus != MH_OK) {
        logger::error("[MinHook] Enable failed: {}", MH_StatusToString(enableStatus));

        return;
    }

    logger::info("All hooks installed.");
}
