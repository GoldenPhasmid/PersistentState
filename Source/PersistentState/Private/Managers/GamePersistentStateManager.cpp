#include "Managers/GamePersistentStateManager.h"

void UGamePersistentStateManager::Init(UGameInstance* InGameInstance)
{
	GameInstance = InGameInstance;
}

void UGamePersistentStateManager::Cleanup(UGameInstance* InGameInstance)
{
	GameInstance = nullptr;
}

