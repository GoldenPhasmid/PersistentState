#include "Managers/PersistentStateManager.h"

void UPersistentStateManager::NotifyObjectInitialized(UObject& Object)
{
	// override in derived classes
}

void UPersistentStateManager::SaveGameState()
{
	// override in derived classes
}
