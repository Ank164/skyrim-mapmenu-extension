#include "Hooks.h"
#include "MapExtensionHint.h"
#include "Scaleform.h"
#include "Utility.h"

namespace Scaleform {
    MapExtensionHint::MapExtensionHint() {
        auto scaleformManager = RE::BSScaleformManager::GetSingleton();
        scaleformManager->LoadMovieEx(this, MENU_PATH, [this](RE::GFxMovieDef* a_def) {
            using StateType = RE::GFxState::StateType;

            fxDelegate.reset(new RE::FxDelegate());
            fxDelegate->RegisterHandler(this);
            a_def->SetState(StateType::kExternalInterface, fxDelegate.get());
            fxDelegate->Release();

            auto logger = new Logger<MapExtensionHint>();
            a_def->SetState(StateType::kLog, logger);
            logger->Release();
        });

        inputContext = Context::kNone;
        depthPriority = 2;

        menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
    }

    void MapExtensionHint::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MapExtensionHint::MENU_NAME, Creator);
            logger::debug("Registered {}", MapExtensionHint::MENU_NAME);
        }
    }

    void MapExtensionHint::Show() {
        auto uiMessageQueue = RE::UIMessageQueue::GetSingleton();
        if (uiMessageQueue) {
            uiMessageQueue->AddMessage(MapExtensionHint::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }
    }

    void MapExtensionHint::Hide() {
        auto uiMessageQueue = RE::UIMessageQueue::GetSingleton();
        if (uiMessageQueue) {
            uiMessageQueue->AddMessage(MapExtensionHint::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
}