#pragma once

#include "CoreMinimal.h"

namespace UE::PersistentState
{
	/** If false, fully disables persistent state subsystem */
	extern bool GPersistentState_Enabled;
	/** If true, persistent state will continuously update stats */
	extern bool GPersistentState_StatsEnabled;
	/** If true, profile state managers are created during Init unless disabled via Project Settings */
	extern bool GPersistentState_CanCreateProfileState;
	/** If true, game state managers are created during Init unless disabled via Project Settings */
	extern bool GPersistentState_CanCreateGameState;
	/** If true, world state managers are created during Init unless disabled via Project Settings */
	extern bool GPersistentState_CanCreateWorldState;
	/** If true, save/load operations run synchronously on game thread by default. Otherwise, UE tasks system is used */
	extern bool GPersistentStateStorage_ForceGameThread;
	/** If true, most recent game state and world state are cached */
	extern bool GPersistentStateStorage_CacheSlotState;
	/** If true, sanitizes outputs invalid object references to the log during saves, editor only */
	extern bool GPersistentState_SanitizeObjectReferences;
	/** formatter type */
	extern int32 GPersistentState_FormatterType;
	
#if !UE_BUILD_SHIPPING
	extern FAutoConsoleCommandWithWorldAndArgs SaveGameToSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs LoadGameFromSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs CreateSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs DeleteSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs DeleteAllSlotsCmd;
	extern FAutoConsoleCommandWithWorld UpdateSlotsCmd;
	extern FAutoConsoleCommandWithWorld ListSlotsCmd;
#endif
}