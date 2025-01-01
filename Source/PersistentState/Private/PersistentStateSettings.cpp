#include "PersistentStateSettings.h"

#include "PersistentStateCVars.h"
#include "PersistentStateSlotStorage.h"
#include "PersistentStateSubsystem.h"

UPersistentStateSettings::UPersistentStateSettings(const FObjectInitializer& Initializer): Super(Initializer)
{
	StateStorageClass = UPersistentStateSlotStorage::StaticClass();
}

bool UPersistentStateSettings::IsDefaultNamedSlot(FName SlotName)
{
	for (const FPersistentSlotEntry& Entry: Get()->DefaultNamedSlots)
	{
		if (Entry.SlotName == SlotName)
		{
			return true;
		}
	}

	return false;
}

FString UPersistentStateSettings::GetSaveGamePath() const
{
	return FPaths::ProjectSavedDir() / SaveGamePath;
}

FString UPersistentStateSettings::GetSaveGameExtension() const
{
	return SaveGameExtension;
}

FString UPersistentStateSettings::GetSaveGameFilePath(FName SlotName) const
{
	const FString SlotFileName = FPaths::SetExtension(SlotName.ToString(), SaveGameExtension);
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SaveGamePath / SlotFileName);
}

FString UPersistentStateSettings::GetScreenshotFilePath(FName SlotName) const
{
	const FString SlotFileName = FPaths::SetExtension(SlotName.ToString() + TEXT("_Screenshot"), ScreenshotExtension);
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SaveGamePath / SlotFileName);
}

EManagerStorageType UPersistentStateSettings::CanCreateManagerState() const
{
	EManagerStorageType CanCreateManagerState = EManagerStorageType::None;
	auto CacheCanCreateFlag = [&CanCreateManagerState](bool Value, EManagerStorageType ManagerType)
	{
		CanCreateManagerState |= (Value ? ManagerType : EManagerStorageType::None);	
	};
	CacheCanCreateFlag(CanCreateProfileState(), EManagerStorageType::Profile);
	CacheCanCreateFlag(CanCreateGameState(), EManagerStorageType::Game);
	CacheCanCreateFlag(CanCreateWorldState(), EManagerStorageType::World);
	
	return CanCreateManagerState;
}

bool UPersistentStateSettings::UseGameThread() const
{
	return bForceGameThread || UE::PersistentState::GPersistentStateStorage_ForceGameThread;
}

bool UPersistentStateSettings::CanCreateProfileState() const
{
	return bStoreProfileState && UE::PersistentState::GPersistentState_CanCreateProfileState;
}

bool UPersistentStateSettings::CanCreateGameState() const
{
	return bStoreGameState && UE::PersistentState::GPersistentState_CanCreateGameState;
}

bool UPersistentStateSettings::CanCreateWorldState() const
{
	return bStoreWorldState && UE::PersistentState::GPersistentState_CanCreateWorldState;
}

bool UPersistentStateSettings::ShouldCacheSlotState() const
{
	return bCacheSlotState && UE::PersistentState::GPersistentStateStorage_CacheSlotState;
}
