#include "Managers/PersistentStateManager.h"

void UPersistentStateManager::NotifyInitialized(UObject& Object)
{
	// override in derived classes
}

void UPersistentStateManager::Tick(float DeltaTime)
{
	// override in derived classes
}

void UPersistentStateManager::SaveGameState()
{
	// override in derived classes
}
