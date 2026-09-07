#include "MapExtension.h"
#include "Scaleform.h"
#include "Utility.h"
#include "Hooks.h"
#include "WorldspaceWarmup.h"

#ifdef PlaySound
#undef PlaySound
#endif

namespace Scaleform {
    MapExtension::MapExtension() {
        auto scaleformManager = RE::BSScaleformManager::GetSingleton();
        scaleformManager->LoadMovieEx(this, MENU_PATH, [this](RE::GFxMovieDef* a_def) {
            using StateType = RE::GFxState::StateType;

            fxDelegate.reset(new RE::FxDelegate());
            fxDelegate->RegisterHandler(this);
            a_def->SetState(StateType::kExternalInterface, fxDelegate.get());
            fxDelegate->Release();

            auto logger = new Logger<MapExtension>();
            a_def->SetState(StateType::kLog, logger);
            logger->Release();
        });

        inputContext = Context::kMap;
        depthPriority = 5;
        menuFlags.set(RE::UI_MENU_FLAGS::kModal, RE::UI_MENU_FLAGS::kTopmostRenderedMenu,
                      RE::UI_MENU_FLAGS::kUsesMenuContext, RE::UI_MENU_FLAGS::kUsesMovementToDirection,
                      RE::UI_MENU_FLAGS::kRequiresUpdate, RE::UI_MENU_FLAGS::kUpdateUsesCursor,
                      RE::UI_MENU_FLAGS::kUsesCursor);
    }

    void MapExtension::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MapExtension::MENU_NAME, Creator);
            logger::debug("Registered {}", MapExtension::MENU_NAME);
        }
    }

    void MapExtension::Show() {
        auto uiMessageQueue = RE::UIMessageQueue::GetSingleton();
        if (uiMessageQueue) {
            uiMessageQueue->AddMessage(MapExtension::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            SetINIValue("fMaxMarkerSelectionDist:MapMenu", 0.0);
        }
    }

    void MapExtension::Hide() {
        auto uiMessageQueue = RE::UIMessageQueue::GetSingleton();
        if (uiMessageQueue) {
            uiMessageQueue->AddMessage(MapExtension::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            SetINIValue("fMaxMarkerSelectionDist:MapMenu", 0.003);
        }
    }

    void MapExtension::Accept(RE::FxDelegateHandler::CallbackProcessor* a_cbReg) {
        a_cbReg->Process("PlaySound", PlayMenuSound);
        a_cbReg->Process("CloseMenu", CloseMenu);
        a_cbReg->Process("ChangeWorld", ChangeWorld);
        a_cbReg->Process("GetStats", GetStats);
    }

    void MapExtension::PlayMenuSound(const RE::FxDelegateArgs& a_params) {
        assert(a_params.GetArgCount() == 1);
        assert(a_params[0].IsString());

        RE::PlaySound(a_params[0].GetString());
    }

    void MapExtension::CloseMenu(const RE::FxDelegateArgs& a_params) {
        assert(a_params.GetArgCount() == 0);

        Hide();
    }

    void MapExtension::ChangeWorld(const RE::FxDelegateArgs& a_params) {
        assert(a_params.GetArgCount() == 2);
        assert(a_params[0].IsString());
        assert(a_params[1].IsString());

        std::string formIDString = a_params[0].GetString();
        RE::FormID formID = static_cast<RE::FormID>(std::stoul(formIDString, nullptr, 16));
        std::string pluginName = a_params[1].GetString();

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        RE::TESWorldSpace* requestedWorld = dataHandler->LookupForm<RE::TESWorldSpace>(formID, pluginName);
        if (!requestedWorld) {
            logger::warn("Couldn't find worldspace {} ({}), closing Map Extension Menu.", formIDString, pluginName);
            Hide();
            return;
        }
        logger::trace("Player requested world {}", requestedWorld->GetName());

        if (requestedWorld == targetWorld) {
            logger::trace("Already showing this world, closing Map Extension Menu");

            Hide();
            return;
        }

        if (pendingWorldSwitch || pendingMapReopen) {
            logger::warn("A world switch is already in progress; ignoring request for '{}'", requestedWorld->GetName());

            Hide();
            return;
        }

        pendingTargetWorld = requestedWorld;
        pendingWorldSwitch = true;

        logger::trace("[WORLD SWITCH] Queued '{}' while currently displaying '{}'", pendingTargetWorld->GetName(),
                      targetWorld ? targetWorld->GetName() : "none");

        Hide();
    }

    void MapExtension::GetStats(const RE::FxDelegateArgs& a_params) {
        assert(a_params.GetArgCount() == 2);
        assert(a_params[0].IsString());
        assert(a_params[1].IsString());

        std::string formIDString = a_params[0].GetString();
        RE::FormID formID = static_cast<RE::FormID>(std::stoul(formIDString, nullptr, 16));
        std::string pluginName = a_params[1].GetString();

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        RE::TESWorldSpace* world = dataHandler->LookupForm<RE::TESWorldSpace>(formID, pluginName);
        if (!world) {
            logger::warn("Couldn't find worldspace {} ({}), can't return stats.", formIDString, pluginName);
            Hide();
            return;
        }
        logger::trace("Stats needed for {}", world->GetName());

        if (const auto menu = RE::UI::GetSingleton()->GetMenu(Scaleform::MapExtension::MENU_NAME); menu) {
            std::array<RE::GFxValue, 6> stats;

            auto discoverStats = GetDiscoverStatsForWorld(world);
            auto questStats = GetQuestStatsForWorld(world);

            logger::trace("Discovered locations: {}/{}", discoverStats.first, discoverStats.second);
            logger::trace("Quests: {} ongoing, {} completed", questStats.first, questStats.second);

            stats[0] = formID;
            stats[1] = pluginName;
            stats[2] = discoverStats.first;
            stats[3] = discoverStats.second;
            stats[4] = questStats.first;
            stats[5] = questStats.second;
            menu->uiMovie->Invoke("_root.MapMenuExtension_mc.SetStats", nullptr, stats.data(), stats.size());
        }
    }
}
