#ifndef HOOKS_H
#define HOOKS_H

#include "RE/M/MapMenu.h"
#include "REL/Relocation.h"

extern bool mapMenuInitialized;
extern bool otherWorld;
extern RE::TESWorldSpace* targetWorld;
extern RE::TESWorldSpace* pendingTargetWorld;

void ResetRuntimeState();
void OpenLocationFinderFromGamepad();
bool NormalizeMapBeforeExtensionOpen();
void Hook_Accept(RE::MapMenu* menu, RE::FxDelegateHandler::CallbackProcessor* processor);
void InstallHooks();

#endif  // HOOKS_H