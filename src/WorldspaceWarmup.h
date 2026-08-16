#pragma once

#include "RE/T/TESWorldSpace.h"

extern std::vector<RE::TESWorldSpace*> g_warmupWorlds;

bool WarmWorldspace(RE::TESWorldSpace* world);
std::vector<RE::TESWorldSpace*> GetWorldspacesMarkedForWarmup();