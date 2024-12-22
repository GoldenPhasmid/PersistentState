#pragma once

#include "CoreMinimal.h"

namespace UE::PersistentState
{
	/** If false, fully disables persistent state subsystem */
	extern bool GPersistentState_Enabled;
	/** If true, save/load operations run synchronously on game thread by default. Otherwise, UE tasks system is used */
	extern bool GPersistentStateStorage_ForceGameThread;
	
#if !UE_BUILD_SHIPPING
	extern FAutoConsoleCommandWithWorldAndArgs SaveGameToSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs LoadGameFromSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs CreateSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs DeleteSlotCmd;
	extern FAutoConsoleCommandWithWorldAndArgs DeleteAllSlotsCmd;
#endif
}