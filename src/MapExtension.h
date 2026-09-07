#ifndef MAP_EXTENSION_H
#define MAP_EXTENSION_H

#include "PCH.h"
#include "RE/G/GFxMovieDef.h"
#include "RE/G/GFxValue.h"
#include "RE/G/GPtr.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "Utility.h"

namespace Scaleform {
    class MapExtension : RE::IMenu {
    public:
        static constexpr const char* MENU_PATH = "mapextension";
        static constexpr const char* MENU_NAME = "MapExtension";

        MapExtension();

        static void Register();
        static void Show();
        static void Hide();

        static constexpr std::string_view Name();

        virtual void Accept(RE::FxDelegateHandler::CallbackProcessor* a_cbReg) override;

        static RE::stl::owner<RE::IMenu*> Creator() { return new MapExtension(); }

    private:
        static void PlayMenuSound(const RE::FxDelegateArgs& a_params);
        static void CloseMenu(const RE::FxDelegateArgs& a_params);
        static void ChangeWorld(const RE::FxDelegateArgs& a_params);
        static void GetStats(const RE::FxDelegateArgs& a_params);
    };

    constexpr std::string_view MapExtension::Name() { return MapExtension::MENU_NAME; }

}

#endif  // MAP_EXTENSION_H
