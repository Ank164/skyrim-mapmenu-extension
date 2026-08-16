#ifndef MAP_EXTENSION_HINT_H
#define MAP_EXTENSION_HINT_H

#include "PCH.h"
#include "RE/G/GFxMovieDef.h"
#include "RE/G/GFxValue.h"
#include "RE/G/GPtr.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "Utility.h"

namespace Scaleform {
    class MapExtensionHint : RE::IMenu {
    public:
        static constexpr const char* MENU_PATH = "mapextensionhint";
        static constexpr const char* MENU_NAME = "MapExtensionHint";

        MapExtensionHint();

        static void Register();
        static void Show();
        static void Hide();

        static constexpr std::string_view Name();

        static RE::stl::owner<RE::IMenu*> Creator() { return new MapExtensionHint(); }
    };

    constexpr std::string_view MapExtensionHint::Name() { return MapExtensionHint::MENU_NAME; }

}

#endif  // MAP_EXTENSION_HINT_H
