#include "PersistentStateSettings.h"

#include "PersistentStateCVars.h"
#include "PersistentStateSlotDescriptor.h"
#include "PersistentStateSlotStorage.h"
#include "PersistentStateSubsystem.h"

UPersistentStateSettings::UPersistentStateSettings(const FObjectInitializer& Initializer): Super(Initializer)
{
	StateStorageClass = UPersistentStateSlotStorage::StaticClass();
	DefaultSlotDescriptor = UPersistentStateSlotDescriptor::StaticClass();
}

void UPersistentStateSettings::PostLoad()
{
	Super::PostLoad();

	for (FPersistentStateDefaultNamedSlot& NamedSlot: DefaultNamedSlots)
	{
		if (NamedSlot.Descriptor == nullptr)
		{
			NamedSlot.Descriptor = DefaultSlotDescriptor;
		}
	}
}

#if WITH_EDITOR
void UPersistentStateSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	for (FPersistentStateDefaultNamedSlot& NamedSlot: DefaultNamedSlots)
	{
		if (NamedSlot.Descriptor == nullptr)
		{
			NamedSlot.Descriptor = DefaultSlotDescriptor;
		}
	}
}
#endif

UPersistentStateSettings* UPersistentStateSettings::GetMutable()
{
	check(IsInGameThread());
	return GetMutableDefault<UPersistentStateSettings>();
}

const UPersistentStateSettings* UPersistentStateSettings::Get()
{
	return GetDefault<UPersistentStateSettings>();
}

FString UPersistentStateSettings::GetSaveGamePath() const
{
	return FPaths::ProjectSavedDir() / SaveGameDirectory;
}

FString UPersistentStateSettings::GetSaveGameExtension() const
{
	return SaveGameExtension;
}

FString UPersistentStateSettings::GetSaveGameFilePath(FName SlotName) const
{
	const FString SlotFileName = FPaths::SetExtension(SlotName.ToString(), SaveGameExtension);
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SaveGameDirectory / SlotFileName);
}

FString UPersistentStateSettings::GetScreenshotFilePath(FName SlotName) const
{
	const FString SlotFileName = FPaths::SetExtension(SlotName.ToString() + TEXT("_Screenshot"), ScreenshotExtension);
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SaveGameDirectory / SlotFileName);
}

bool UPersistentStateSettings::IsEnabled() const
{
	return bEnabled;
}

bool UPersistentStateSettings::HasValidConfiguration() const
{
	return StateStorageClass != nullptr && DefaultSlotDescriptor != nullptr;
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
