#pragma once

#include "CoreMinimal.h"
#include "SaveGameSystem.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

DECLARE_STATS_GROUP(TEXT("Persistent State"), STATGROUP_PersistentState, STATCAT_Advanced);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Total Tracked Objects"), STAT_PersistentState_NumObjects, STATGROUP_PersistentState, PERSISTENTSTATE_API)

CSV_DECLARE_CATEGORY_EXTERN(PersistentState);

DECLARE_LOG_CATEGORY_EXTERN(LogPersistentState, Log, All);
UE_TRACE_CHANNEL_EXTERN(PersistentStateChannel);

class PERSISTENTSTATE_API IPersistentStateModule: public ISaveGameSystemModule
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

