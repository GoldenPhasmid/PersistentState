#include "PersistentStateSlotStorage.h"

#include "PersistentStateDefines.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"

namespace UE::PersistentState
{
	FString GCurrentWorldPackage;	
}

void UPersistentStateSlotStorage::Init()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_Init, PersistentStateChannel);
	
	check(StateSlots.IsEmpty());
	for (const FPersistentSlotEntry& Entry: UPersistentStateSettings::Get()->PersistentSlots)
	{
		FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(Entry.SlotName.ToString(), Entry.Title);
		StateSlots.Add(Slot);
	}

	RefreshSlots();
}

void UPersistentStateSlotStorage::Shutdown()
{
	// @todo: force save operation if saving async
}

void UPersistentStateSlotStorage::RefreshSlots()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_RefreshSlots, PersistentStateChannel);

	TArray<FString> SaveGameFiles = GetSaveGameFiles();

	TArray<FName> SaveGameNames;
	SaveGameNames.Reserve(SaveGameFiles.Num());
	for (const FString& File: SaveGameFiles)
	{
		
		SaveGameNames.Add(FName{FPaths::GetBaseFilename(File)});
	}
	
	TArray<bool> SaveGameNameStatus;
	SaveGameNameStatus.SetNum(SaveGameFiles.Num());

	auto Settings = UPersistentStateSettings::Get();

	// match state slots with save game files, remove non-persistent slots that doesn't have a valid save file
	for (auto It = StateSlots.CreateIterator(); It; ++It)
	{
		FPersistentStateSlotSharedRef Slot = *It;
		const FName SlotName = Slot->GetSlotName();

		// slot name is matched to the file name. Maybe this is not always a desired behavior, but it is what it is
		int32 SaveGameIndex = SaveGameNames.IndexOfByKey(SlotName);
		if (SaveGameIndex != INDEX_NONE)
		{
			SaveGameNameStatus[SaveGameIndex] = true;
			if (!Slot->HasFilePath())
			{
				const FString& FilePath = SaveGameFiles[SaveGameIndex];

				// @todo: verify that CreateReadArchive is not a costly operation
				TUniquePtr<FArchive> ReadArchive = CreateSaveGameReader(FilePath);
				Slot->TrySetFilePath(*ReadArchive, SaveGameFiles[SaveGameIndex]);
			}
		}

		if (SaveGameIndex == INDEX_NONE || !Slot->IsValidSlot())
		{
			if (Settings->IsPersistentSlot(Slot->GetSlotName()))
			{
				// reset file path on a persistent slot
				Slot->ResetFileData();
			}
			else
			{
				// failed to match state slot with a save game file, or save game file is corrupted
				It.RemoveCurrentSwap();
			}
		}
	}

	// process remaining save game files
	for (int32 Index = 0; Index < SaveGameNameStatus.Num(); ++Index)
	{
		if (SaveGameNameStatus[Index] == true)
		{
			// file name is assigned to a state slot
			continue;
		}

		// @todo: verify that CreateReadArchive is not a costly operation
		TUniquePtr<FArchive> ReadArchive = CreateSaveGameReader(SaveGameFiles[Index]);
		FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(*ReadArchive, SaveGameFiles[Index]);
		if (Slot->IsValidSlot())
		{
			// create state slot if file is valid
			StateSlots.Add(Slot);
		}
	}
}

FPersistentStateSlotHandle UPersistentStateSlotStorage::CreateStateSlot(const FString& SlotName, const FText& Title)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_CreateStateSlot, PersistentStateChannel);
	
	if (FPersistentStateSlotSharedRef Slot = FindSlot(FName{SlotName}); Slot.IsValid())
	{
		ensureAlwaysMsgf(false, TEXT("%s: trying to create slot with name %s that already exists."), *FString(__FUNCTION__), *SlotName);
		return FPersistentStateSlotHandle{*this, *Slot};
	}

	FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(SlotName, Title);
	CreateSaveGameFile(Slot);
	StateSlots.Add(Slot);
	
	return FPersistentStateSlotHandle{*this, *Slot};
}

void UPersistentStateSlotStorage::GetAvailableSlots(TArray<FPersistentStateSlotHandle>& OutStates)
{
	OutStates.Reset(StateSlots.Num());
	Algo::Transform(StateSlots, OutStates, [this](const FPersistentStateSlotSharedRef& Slot)
	{
		return FPersistentStateSlotHandle{*this, *Slot};
	});
}

FPersistentStateSlotHandle UPersistentStateSlotStorage::GetStateBySlotName(FName SlotName) const
{
	FPersistentStateSlotSharedRef Slot = FindSlot(SlotName);
	if (Slot.IsValid())
	{
		return FPersistentStateSlotHandle{*this, *Slot};
	}

	return FPersistentStateSlotHandle::InvalidHandle;
}

FPersistentStateSlotSharedRef UPersistentStateSlotStorage::GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	return FindSlot(SlotHandle.GetSlotName());
}

bool UPersistentStateSlotStorage::CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle.GetSlotName());
	if (!StateSlot.IsValid())
	{
		return false;
	}
	
	return HasSaveGameFile(StateSlot);
}

bool UPersistentStateSlotStorage::CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle.GetSlotName());
	if (!StateSlot.IsValid())
	{
		return false;
	}
	
	const FName SlotName = StateSlot->GetSlotName();
	if (UPersistentStateSettings::Get()->IsPersistentSlot(SlotName))
	{
		return true;
	}

	return HasSaveGameFile(StateSlot);
}

void UPersistentStateSlotStorage::RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
{
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle.GetSlotName());
	if (!StateSlot.IsValid())
	{
		return;
	}

	if (const FString& FilePath = StateSlot->GetFilePath(); !FilePath.IsEmpty())
	{
		RemoveSaveGameFile(StateSlot->GetFilePath());
	}
	
	StateSlots.RemoveSwap(StateSlot);
}

void UPersistentStateSlotStorage::SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_SaveWorldStateImpl, PersistentStateChannel);

	FPersistentStateSlotSharedRef SourceSlot = FindSlot(SourceSlotHandle.GetSlotName());
	check(SourceSlot.IsValid());
	
	FPersistentStateSlotSharedRef TargetSlot = FindSlot(TargetSlotHandle.GetSlotName());
	check(TargetSlot.IsValid());

	CurrentSlotHandle = TargetSlotHandle;
	CurrentWorldState = WorldState;

	if (!TargetSlot->HasFilePath())
	{
		check(UPersistentStateSettings::Get()->IsPersistentSlot(TargetSlot->GetSlotName()));
		CreateSaveGameFile(TargetSlot);
	}

	TargetSlot->SaveWorldState(
		WorldState,
		[this](const FString& FilePath) { return CreateSaveGameReader(FilePath); },
		[this](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
	);
	
	// @todo: write new world state from TargetSlot and other data from SourceSlot to a TargetSlot file path
	// file path should be created if necessary
}

FWorldStateSharedRef UPersistentStateSlotStorage::LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_LoadWorldStateImpl, PersistentStateChannel);
	
	FPersistentStateSlotSharedRef CurrentStateSlot = FindSlot(CurrentSlotHandle.GetSlotName());
	if (!CurrentStateSlot.IsValid())
	{
		return {};
	}

	if (!CurrentStateSlot->HasFilePath() == false)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Trying to load world state %s from a slot %s that doesn't have associated file path."),
			*FString(__FUNCTION__), *WorldToLoad.ToString(), *CurrentSlotHandle.GetSlotName().ToString());
		return {};
	}
	
	if (CurrentSlotHandle == TargetSlotHandle && CurrentWorldState.IsValid() && CurrentWorldState->GetWorld() == WorldToLoad)
	{
		// if we're loading the same world from the same slot, we can reuse previously loaded world state
		return CurrentWorldState;
	}
	
	TUniquePtr<FArchive> DataReader = CreateSaveGameReader(CurrentStateSlot->GetFilePath());

	CurrentSlotHandle = TargetSlotHandle;
	CurrentWorldState = CurrentStateSlot->LoadWorldState(*DataReader, WorldToLoad);
	check(CurrentWorldState.IsValid());

	return CurrentWorldState;
}

FPersistentStateSlotSharedRef UPersistentStateSlotStorage::FindSlot(FName SlotName) const
{
	if (const FPersistentStateSlotSharedRef* Slot = StateSlots.FindByPredicate([SlotName](const FPersistentStateSlotSharedRef& Slot)
	{
		return Slot->GetSlotName() == SlotName;
	}))
	{
		return *Slot;
	}

	return {};
}

bool UPersistentStateSlotStorage::HasSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const
{
	return Slot->HasFilePath() && IFileManager::Get().FileExists(*Slot->GetFilePath());
}

void UPersistentStateSlotStorage::CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const
{
	// create file and associate it with a slot
	auto Settings = UPersistentStateSettings::Get();
	const FName SlotName = Slot->GetSlotName();
	
	const FString FilePath = Settings->GetSaveGameFilePath(SlotName);
	Slot->SetFilePath(FilePath);
	TUniquePtr<FArchive> Archive = CreateSaveGameWriter(FilePath);
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameReader(const FString& FilePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_CreateReadArchive, PersistentStateChannel);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameWriter(const FString& FilePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_CreateWriteArchive, PersistentStateChannel);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent | FILEWRITE_EvenIfReadOnly)};
}

TArray<FString> UPersistentStateSlotStorage::GetSaveGameFiles() const
{
	auto Settings = UPersistentStateSettings::Get();
	
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *Settings->GetSaveGamePath(), *Settings->GetSaveGameExtension());

	return FoundFiles;
}

void UPersistentStateSlotStorage::RemoveSaveGameFile(const FString& FilePath)
{
	IFileManager::Get().Delete(*FilePath, true, false, true);
}

