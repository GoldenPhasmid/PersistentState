#include "Managers/PersistentStateManager.h"

#include "PersistentStateSubsystem.h"

UWorld* UPersistentStateManager::GetWorld() const
{
	return GetTypedOuter<UPersistentStateSubsystem>()->GetWorld();
}

bool UPersistentStateManager::ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const
{
	// override in derived classes
	return true;
}

void UPersistentStateManager::Init(UPersistentStateSubsystem& InSubsystem)
{
	// override in derived classes
}

void UPersistentStateManager::Cleanup(UPersistentStateSubsystem& InSubsystem)
{
	// override in derived classes
}

void UPersistentStateManager::NotifyObjectInitialized(UObject& Object)
{
	// override in derived classes
}

void UPersistentStateManager::SaveState()
{
	// override in derived classes
}

UPersistentStateSubsystem* UPersistentStateManager::GetStateSubsystem() const
{
	return GetTypedOuter<UPersistentStateSubsystem>();
}
