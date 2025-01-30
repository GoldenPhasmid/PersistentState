#include "PersistentStateSlotView.h"

#include "PersistentStateSlot.h"
#include "PersistentStateStorage.h"

FPersistentStateSlotHandle FPersistentStateSlotHandle::InvalidHandle;

FPersistentStateSlotHandle::FPersistentStateSlotHandle(const UPersistentStateStorage& InStorage, const FName& InSlotName)
	: SlotName(InSlotName)
	, WeakStorage(&InStorage)
{
	check(SlotName != NAME_None);
}

bool FPersistentStateSlotHandle::IsValid() const
{
	if (SlotName == NAME_None)
	{
		return false;
	}
	
	if (const UPersistentStateStorage* StateStorage = WeakStorage.Get())
	{
		return *this == StateStorage->GetStateSlotByName(SlotName);
	}

	return false;
}

FPersistentStateSlotDesc::FPersistentStateSlotDesc(const FPersistentStateSlot& Slot)
	: SlotName(Slot.GetSlotName())
	, SlotTitle(Slot.GetSlotTitle())
	, FilePath(Slot.GetFilePath())
	, LastSaveTimestamp(Slot.GetTimeStamp())
	, LastSavedWorld(Slot.GetLastSavedWorld())
{
	Slot.GetSavedWorlds(SavedWorlds);
}

FString FPersistentStateSlotDesc::ToString() const
{
	return FString::Printf(TEXT("Name: %s, Title: %s, FilePath: %s, Saved World: %s"),
		*SlotName.ToString(), *SlotTitle.ToString(), *FPaths::ConvertRelativePathToFull(FilePath), *LastSavedWorld.ToString());
}
