#include "PersistentStateSlotStorage.h"

#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"

void UPersistentStateSlotStorage::Init()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_Init, PersistentStateChannel);
	
	check(StateSlots.IsEmpty());
	for (const FPersistentSlotEntry& Entry: UPersistentStateSettings::Get()->DefaultSlots)
	{
		CreateStateSlot(Entry.SlotName, Entry.Title);
	}

	// @todo: implement save files discovery
}

void UPersistentStateSlotStorage::Shutdown()
{
	// force save operation if async
}

void UPersistentStateSlotStorage::RefreshSlots()
{
	// @todo: implement save files discovery
}

FPersistentStateSlotHandle UPersistentStateSlotStorage::CreateStateSlot(const FString& SlotName, const FText& Title)
{
	TSharedPtr<FPersistentStateSlot> Slot = FindSlot(FName{SlotName});
	if (ensureAlwaysMsgf(!Slot.IsValid(), TEXT("%s: trying to create slot with name %s that already exists."), *FString(__FUNCTION__), *SlotName))
	{
		Slot = MakeShared<FPersistentStateSlot>(SlotName, Title);
		StateSlots.Add(Slot);

		return FPersistentStateSlotHandle{*this, *Slot};
	}

	return FPersistentStateSlotHandle::InvalidHandle;
}

void UPersistentStateSlotStorage::GetAvailableSlots(TArray<FPersistentStateSlotHandle>& OutStates)
{
	OutStates.Reset(StateSlots.Num());
	Algo::Transform(StateSlots, OutStates, [this](const TSharedPtr<FPersistentStateSlot>& Slot)
	{
		return FPersistentStateSlotHandle{*this, *Slot};
	});
}

FPersistentStateSlotHandle UPersistentStateSlotStorage::GetStateBySlotName(FName SlotName) const
{
	TSharedPtr<FPersistentStateSlot> Slot = FindSlot(SlotName);
	if (Slot.IsValid())
	{
		return FPersistentStateSlotHandle{*this, *Slot};
	}

	return FPersistentStateSlotHandle::InvalidHandle;
}

TSharedPtr<FPersistentStateSlot> UPersistentStateSlotStorage::GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	return FindSlot(SlotHandle.GetSlotName());
}

void UPersistentStateSlotStorage::SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_SaveWorldStateImpl, PersistentStateChannel);

	TSharedPtr<FPersistentStateSlot> SourceSlot = FindSlot(SourceSlotHandle.GetSlotName());
	check(SourceSlot.IsValid());
	
	TSharedPtr<FPersistentStateSlot> TargetSlot = FindSlot(TargetSlotHandle.GetSlotName());
	check(TargetSlot.IsValid());
	
	PreLoadSlot = TargetSlotHandle;
	TargetSlot->SetWorldState(WorldState);
	TargetSlot->SetLastSavedWorld(WorldState->World);
	
	// @todo: write from SourceSlot to TargetSlot file
}

FWorldStateSharedRef UPersistentStateSlotStorage::LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_LoadWorldStateImpl, PersistentStateChannel);
	
	TSharedPtr<FPersistentStateSlot> Slot = FindSlot(PreLoadSlot.GetSlotName());
	check(Slot.IsValid());
	
	if (PreLoadSlot == TargetSlotHandle)
	{
		if (FWorldStateSharedRef WorldState = Slot->GetWorldState(); WorldState.IsValid() && WorldState->GetWorld() == WorldToLoad)
		{
			// if we're loading the same world from the same slot, we can reuse previously loaded world state
			return WorldState;
		}
	}
	
	TUniquePtr<FArchive> DataReader = CreateReadArchive(Slot->FilePath);

	PreLoadSlot = TargetSlotHandle;
	Slot->SetWorldState(Slot->LoadWorldState(*DataReader, WorldToLoad));

	return Slot->GetWorldState();
}

TSharedPtr<FPersistentStateSlot> UPersistentStateSlotStorage::FindSlot(FName SlotName) const
{
	if (const TSharedPtr<FPersistentStateSlot>* Slot = StateSlots.FindByPredicate([SlotName](const TSharedPtr<FPersistentStateSlot>& Slot)
	{
		return Slot->GetSlotName() == SlotName;
	}))
	{
		return *Slot;
	}

	return {};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateReadArchive(const FString& FilePath) const
{
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateWriteArchive(const FString& FilePath) const
{
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent)};
}
