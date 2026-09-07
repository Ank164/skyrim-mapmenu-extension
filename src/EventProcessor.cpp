#include "EventProcessor.h"

#include "Hooks.h"
#include "MapExtension.h"
#include "MapExtensionHint.h"
#include "Utility.h"

namespace {
    bool g_mapExtensionOpenQueued = false;

    void CloseJournalMenuIfOpen() {
        auto* ui = RE::UI::GetSingleton();

        if (!ui || !ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
            return;
        }

        auto* queue = RE::UIMessageQueue::GetSingleton();

        if (!queue) {
            return;
        }

        queue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }

    void QueueMapExtensionOpen() {
        if (g_mapExtensionOpenQueued) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();

        if (!tasks) {
            logger::error("[MapExtension] Task interface unavailable");

            return;
        }

        g_mapExtensionOpenQueued = true;

        tasks->AddUITask([]() {
            auto* tasks = SKSE::GetTaskInterface();

            if (!tasks) {
                g_mapExtensionOpenQueued = false;
                return;
            }

            tasks->AddUITask([]() {
                g_mapExtensionOpenQueued = false;

                auto* ui = RE::UI::GetSingleton();

                if (!ui || !ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ||
                    ui->IsMenuOpen(Scaleform::MapExtension::MENU_NAME)) {
                    return;
                }

                logger::trace(
                    "[MapExtension] Opening after World Map "
                    "input context was restored");

                Scaleform::MapExtension::Show();
            });
        });
    }
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                                      RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (!event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (event->opening) {
        if (event->menuName == Scaleform::MapExtensionHint::MENU_NAME) {
            if (const auto extensionHint = RE::UI::GetSingleton()->GetMenu(Scaleform::MapExtensionHint::MENU_NAME);
                extensionHint) {
                std::array<RE::GFxValue, 3> buttons;
                buttons[0] = RE::BSInputDeviceManager::GetSingleton()->IsGamepadEnabled();
                buttons[1] = menuHotkey;
                buttons[2] = menuGamepadHotkey;
                extensionHint->uiMovie->Invoke("_root.MapExtensionHint_mc.setButton", nullptr, buttons.data(),
                                               buttons.size());

                RE::GViewport vp{};
                extensionHint->uiMovie->GetViewport(&vp);

                if (vp.height > 0) {
                    const float aspect = static_cast<float>(vp.width) / static_cast<float>(vp.height);
                    g_isUltraWide = aspect > 1.95f;
                } else {
                    g_isUltraWide = false;
                }

                RE::GFxValue arg;
                arg.SetBoolean(g_isUltraWide);

                extensionHint->uiMovie->Invoke("_root.MapExtensionHint_mc.setWidescreen", nullptr, &arg, 1);
            }
        } else if (event->menuName == RE::MapMenu::MENU_NAME) {
            if (pendingMapReopen) {
                pendingMapReopen = false;
                logger::info("[WORLD SWITCH] Replacement MapMenu opened for '{}'",
                             targetWorld && targetWorld->GetName() ? targetWorld->GetName() : "<null>");
            }

            Scaleform::MapExtensionHint::Show();
        } else if (event->menuName == Scaleform::MapExtension::MENU_NAME) {
            if (const auto extensionMenu = RE::UI::GetSingleton()->GetMenu(Scaleform::MapExtension::MENU_NAME);
                extensionMenu) {
                // FAST TRAVEL SECTION
                RE::GFxValue worldsArray;
                extensionMenu->uiMovie->CreateArray(&worldsArray);
                for (const auto& data : worldMapsData) {
                    RE::GFxValue worldOption;
                    extensionMenu->uiMovie->CreateObject(&worldOption);

                    worldOption.SetMember("worldName", data["worldName"].get<std::string>().c_str());
                    worldOption.SetMember("worldID", data["worldID"].get<std::string>().c_str());
                    worldOption.SetMember("mapRegion", data["mapRegion"].get<std::string>().c_str());
                    worldOption.SetMember("plugin", data["plugin"].get<std::string>().c_str());
                    worldOption.SetMember("description", data["description"].get<std::string>().c_str());
                    worldOption.SetMember("symbol", data["symbol"].get<int>());

                    worldsArray.PushBack(worldOption);
                }
                std::array<RE::GFxValue, 1> worldsData;
                worldsData[0] = worldsArray;
                extensionMenu->uiMovie->Invoke("_root.MapMenuExtension_mc.SetWorldOptions", nullptr, worldsData.data(),
                                               worldsData.size());

                // TACTICAL SECTION
                RE::GFxValue regionsArray;
                extensionMenu->uiMovie->CreateArray(&regionsArray);

                for (const auto& data : tacticalRegionsData) {
                    const std::string factionID = ResolveRegionFactionID(data);

                    const nlohmann::json* factionData = nullptr;

                    if (!factionID.empty()) {
                        const auto factionIt = tacticalFactionsByID.find(factionID);

                        if (factionIt != tacticalFactionsByID.end()) {
                            factionData = &factionIt->second;
                        } else {
                            logger::warn("Unknown resolved faction '{}' for tactical region '{}:{}'", factionID,
                                         data.value("provinceName", ""), data.value("id", ""));
                        }
                    }

                    RE::GFxValue regionOption;
                    extensionMenu->uiMovie->CreateObject(&regionOption);

                    regionOption.SetMember("regionName", data["id"].get<std::string>().c_str());
                    regionOption.SetMember("provinceName", data["provinceName"].get<std::string>().c_str());
                    if (factionData) {
                        regionOption.SetMember("factionName", (*factionData)["name"].get<std::string>().c_str());

                        regionOption.SetMember("factionColor", (*factionData)["colorMain"].get<std::string>().c_str());
                    }

                    CrimeFactionInfo crimeInfo{};

                    if (data.contains("crime") && data["crime"].is_string()) {
                        const std::string crimeFactionEditorID = data["crime"].get<std::string>();

                        if (!crimeFactionEditorID.empty()) {
                            crimeInfo = GetCrimeFactionInfo(crimeFactionEditorID);
                        }
                    }

                    regionOption.SetMember("crime", crimeInfo.crimeGold);
                    regionOption.SetMember("crimeType", crimeInfo.crimeType.c_str());

                    if (data.contains("survival") && data["survival"].is_object()) {
                        const auto season = static_cast<std::uint32_t>(GetCurrentSeason());

                        if (season >= 1 && season <= 4) {
                            const auto seasonKey = std::to_string(season);
                            const auto& survival = data["survival"];

                            const auto climateID = survival.at(seasonKey).get<std::string>();

                            const auto climateIt = tacticalClimatesByID.find(climateID);

                            if (climateIt != tacticalClimatesByID.end()) {
                                const auto& climateData = climateIt->second;

                                regionOption.SetMember("survivalName", climateData["name"].get<std::string>().c_str());

                                regionOption.SetMember("survivalColor",
                                                       climateData["colorMain"].get<std::string>().c_str());
                            } else {
                                logger::warn("Unknown climate '{}' for tactical region '{}:{}'", climateID,
                                             data.value("provinceName", ""), data.value("id", ""));
                            }
                        } else {
                            logger::warn("Invalid current season '{}' for tactical region '{}:{}'", season,
                                         data.value("provinceName", ""), data.value("id", ""));
                        }
                    }

                    regionsArray.PushBack(regionOption);
                }
                std::array<RE::GFxValue, 1> regionsData;
                regionsData[0] = regionsArray;
                extensionMenu->uiMovie->Invoke("_root.MapMenuExtension_mc.SetTacticalRegions", nullptr,
                                               regionsData.data(), regionsData.size());

                std::array<RE::GFxValue, 2> seasonsData;
                seasonsData[0] = IsSeasonsInstalled();
                seasonsData[1] = GetCurrentSeason();
                extensionMenu->uiMovie->Invoke("_root.MapMenuExtension_mc.SetSeasonText", nullptr, seasonsData.data(),
                                               seasonsData.size());

                // Setting keys/buttons
                logger::trace("Setting gamepad.");
                std::array<RE::GFxValue, 11> gamepad;
                gamepad[0] = RE::BSInputDeviceManager::GetSingleton()->IsGamepadEnabled();
                gamepad[1] = menuHotkey;
                gamepad[2] = menuGamepadHotkey;
                gamepad[3] = regionsKey;
                gamepad[4] = regionsGamepadKey;
                gamepad[5] = mapModeKey;
                gamepad[6] = mapModeGamepadKey;
                gamepad[7] = leftKey;
                gamepad[8] = leftGamepadKey;
                gamepad[9] = rightKey;
                gamepad[10] = rightGamepadKey;
                extensionMenu->uiMovie->Invoke("_root.MapMenuExtension_mc.SetGamepad", nullptr, gamepad.data(),
                                               gamepad.size());
            }
        }
    } else if (!event->opening && event->menuName == Scaleform::MapExtension::MENU_NAME) {
        if (pendingWorldSwitch) {
            pendingWorldSwitch = false;
            pendingMapReopen = true;

            auto* queue = RE::UIMessageQueue::GetSingleton();

            if (!queue) {
                logger::error("[WORLD SWITCH] UI message queue unavailable");

                pendingMapReopen = false;

                return RE::BSEventNotifyControl::kContinue;
            }

            logger::trace("[WORLD SWITCH] Queuing MapMenu hide");

            queue->AddMessage(RE::MapMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    } else if (!event->opening && event->menuName == RE::MapMenu::MENU_NAME) {
        Scaleform::MapExtensionHint::Hide();

        mapMenuInitialized = false;
        cloudSwapPending = false;

        if (otherWorld && backup) {
            RestoreWorldspaceState(backup);

            otherWorld = false;
            backup = nullptr;
        }

        if (pendingMapReopen) {
            if (!pendingTargetWorld) {
                pendingMapReopen = false;
                pendingWorldSwitch = false;
                return RE::BSEventNotifyControl::kContinue;
            }

            targetWorld = pendingTargetWorld;
            pendingTargetWorld = nullptr;
            pendingWorldSwitch = false;
            g_mapWorldOverrideActive = true;

            logger::trace("[WorldResolver] Override armed for '{}' before MapMenu reopen",
                          targetWorld ? targetWorld->GetName() : "<null>");

            auto* tasks = SKSE::GetTaskInterface();

            if (!tasks) {
                logger::error("[WORLD SWITCH] Task interface unavailable");
                pendingMapReopen = false;

                return RE::BSEventNotifyControl::kContinue;
            }

            // UI tasks queued from another UI task can be drained in the same frame.
            // Give the renderer real time to release the old map scene before asking
            // the UI thread to create the replacement MapMenu.
            logger::info("[WORLD SWITCH] Waiting 350 ms for old map scene teardown");
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(350));

                auto* tasks = SKSE::GetTaskInterface();

                if (!tasks) {
                    logger::error("[WORLD SWITCH] Task interface unavailable during deferred reopen");
                    return;
                }

                tasks->AddUITask([]() {
                    auto* ui = RE::UI::GetSingleton();

                    if (!ui) {
                        logger::error("[WORLD SWITCH] UI unavailable");
                        return;
                    }

                    if (ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
                        logger::error(
                            "[WORLD SWITCH] Old MapMenu is still open after deferred close; "
                            "reopen aborted");
                        return;
                    }

                    auto* queue = RE::UIMessageQueue::GetSingleton();

                    if (!queue) {
                        logger::error("[WORLD SWITCH] UI message queue unavailable");
                        return;
                    }

                    logger::info("[WORLD SWITCH] Reopening MapMenu after timed scene teardown");
                    queue->AddMessage(RE::MapMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                });
            }).detach();
        } else {
            g_mapWorldOverrideActive = false;

            pendingTargetWorld = nullptr;
            pendingWorldSwitch = false;

            auto* player = RE::PlayerCharacter::GetSingleton();

            auto* playerWorld = player ? player->GetWorldspace() : nullptr;

            if (playerWorld && playerWorld->parentWorld) {
                playerWorld = playerWorld->parentWorld;
            }

            targetWorld = playerWorld;
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(RE::InputEvent* const* eventPtr,
                                                      RE::BSTEventSource<RE::InputEvent*>*) {
    auto* main = RE::Main::GetSingleton();
    if (!eventPtr || !*eventPtr || !main || !main->GetRuntimeData().gameActive) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* event = *eventPtr;

    if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
        auto* buttonEvent = event->AsButtonEvent();

        if (buttonEvent && buttonEvent->IsDown()) {
            auto* ui = RE::UI::GetSingleton();

            const auto device = buttonEvent->device.get();
            const auto code = buttonEvent->GetIDCode();

            if (ui && ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
                const auto configuredGamepadMask = GamepadKeycodeToMask(menuGamepadHotkey);

                const bool openExtensionInput =
                    (device == RE::INPUT_DEVICE::kKeyboard && code == static_cast<std::uint32_t>(menuHotkey)) ||
                    (device == RE::INPUT_DEVICE::kGamepad && code == configuredGamepadMask);

                if (openExtensionInput) {
                    CloseJournalMenuIfOpen();

                    const bool normalized = NormalizeMapBeforeExtensionOpen();

                    if (normalized) {
                        logger::trace(
                            "[MapExtension] Map state normalized before "
                            "extension opening");
                    }

                    QueueMapExtensionOpen();

                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto findLocationGamepadMask = GamepadKeycodeToMask(273);

                if (device == RE::INPUT_DEVICE::kGamepad && code == findLocationGamepadMask) {
                    OpenLocationFinderFromGamepad();
                }
            }
        }
    }

    // this is used to reset the map to the actual player's world once the menu is closed
    // had to do it this way to not interfer with world swaps between two non-current worlds (achieved by reopening the
    // menu with UI messages)
    auto* ui = RE::UI::GetSingleton();

    if (ui && !ui->IsMenuOpen(RE::MapMenu::MENU_NAME) && !otherWorld && !pendingWorldSwitch && !pendingMapReopen) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerWorld = player ? player->GetWorldspace() : nullptr;

        if (playerWorld && playerWorld->parentWorld) {
            playerWorld = playerWorld->parentWorld;
        }

        if (targetWorld != playerWorld) {
            targetWorld = playerWorld;
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventProcessor::ProcessEvent(const RE::TESFastTravelEndEvent* event,
                                                      RE::BSTEventSource<RE::TESFastTravelEndEvent>*) {
    if (!event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    logger::trace("Player fast traveled, resetting target world.");

    auto* player = RE::PlayerCharacter::GetSingleton();

    targetWorld = player ? player->GetWorldspace() : nullptr;

    if (targetWorld && targetWorld->parentWorld) {
        targetWorld = targetWorld->parentWorld;
    }

    pendingTargetWorld = nullptr;
    otherWorld = false; 
    pendingWorldSwitch = false;
    pendingMapReopen = false;
    cloudSwapPending = false;
    backup = nullptr;

    return RE::BSEventNotifyControl::kContinue;
}
