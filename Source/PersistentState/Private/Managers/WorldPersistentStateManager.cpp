#include "Managers/WorldPersistentStateManager.h"

bool UWorldPersistentStateManager::ShouldCreateManager(UWorld* InWorld) const
{
	return InWorld && InWorld->IsGameWorld();
}

void UWorldPersistentStateManager::Init(UWorld* InWorld)
{
	CurrentWorld = InWorld;
}

void UWorldPersistentStateManager::Cleanup(UWorld* InWorld)
{
	check(CurrentWorld == InWorld);
	CurrentWorld = nullptr;
}
