#include "Managers/WorldPersistentStateManager.h"

void UWorldPersistentStateManager::Init(UWorld* InWorld)
{
	CurrentWorld = InWorld;
}

void UWorldPersistentStateManager::Cleanup(UWorld* InWorld)
{
	check(CurrentWorld == InWorld);
	CurrentWorld = nullptr;
}
