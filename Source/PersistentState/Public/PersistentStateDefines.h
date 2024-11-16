#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class PERSISTENTSTATE_API IPersistentStateModule
{
	static IPersistentStateModule& Get()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_PersistentStateModule_Get);
		static IPersistentStateModule& Module = FModuleManager::Get().LoadModuleChecked<IPersistentStateModule>("PersistentState");
		return Module;
	}

	static bool IsLoaded()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_PersistentStateModule_IsLoaded);
		return FModuleManager::Get().IsModuleLoaded("PersistentState");
	}
	
};

/** log category */
DECLARE_LOG_CATEGORY_EXTERN(LogPersistentState, Log, All);
/** trace channel */
UE_TRACE_CHANNEL_EXTERN(PersistentStateChannel);