#include "PersistentStateSettings.h"

#include "PersistentStateSlotStorage.h"
#include "PersistentStateStorage.h"

UPersistentStateSettings::UPersistentStateSettings(const FObjectInitializer& Initializer): Super(Initializer)
{
	StateStorageClass = UPersistentStateSlotStorage::StaticClass();
}

bool UPersistentStateSettings::IsDefaultNamedSlot(FName SlotName) const
{
	for (const FPersistentSlotEntry& Entry: DefaultNamedSlots)
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
	return FPaths::ProjectSavedDir() / SaveGamePath / SlotFileName;
}
