#include "PersistentStateSlotStorage.h"

#include "PersistentStateCVars.h"
#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"

namespace UE::PersistentState
{
	FString GCurrentWorldPackage;
}

UPersistentStateSlotStorage::UPersistentStateSlotStorage(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, TaskPipe{TEXT("PersistentStatePipe")}
{
	
}
UPersistentStateSlotStorage::UPersistentStateSlotStorage(FVTableHelper& Helper)
	: Super(Helper)
	, TaskPipe{TEXT("PersistentStatePipe")}
{
	
}
void UPersistentStateSlotStorage::Init()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	auto Settings = UPersistentStateSettings::Get();
	check(StateSlots.IsEmpty());
	for (const FPersistentSlotEntry& Entry: Settings->DefaultNamedSlots)
	{
		FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(Entry.SlotName, Entry.Title);
		StateSlots.Add(Slot);
	}

	// check that save directory exists or create it if it doesn't
	const FString SaveGamePath = Settings->GetSaveGamePath();
	if (!IFileManager::Get().DirectoryExists(*SaveGamePath))
	{
		IFileManager::Get().MakeDirectory(*SaveGamePath, true);
	}

	UpdateAvailableStateSlots();
}

void UPersistentStateSlotStorage::Shutdown()
{
	TaskPipe.WaitUntilEmpty();
}

uint32 UPersistentStateSlotStorage::GetAllocatedSize() const
{
	uint32 TotalMemory = 0;
#if STATS
	TotalMemory += GetClass()->GetStructureSize();
	TotalMemory += StateSlots.GetAllocatedSize();
	TotalMemory += sizeof(FPersistentStateSlot) * StateSlots.Num();

	for (const FPersistentStateSlotSharedRef& StateSlot: StateSlots)
	{
		TotalMemory += StateSlot->GetAllocatedSize();
	}

	if (CurrentGameState.IsValid())
	{
		TotalMemory += CurrentGameState->GetAllocatedSize();
	}
	if (CurrentWorldState.IsValid())
	{
		TotalMemory += CurrentWorldState->GetAllocatedSize();
	}
#endif
	
	return TotalMemory;
}

UE::Tasks::FTask UPersistentStateSlotStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
{
	check(IsInGameThread());
	check(GameState.IsValid() && WorldState.IsValid());

	// @todo: data compression for GameState/WorldState
	auto TaskBody = [this, GameState, WorldState, SourceSlotHandle, TargetSlotHandle, CompletedDelegate]
	{
		SaveState(GameState, WorldState, SourceSlotHandle, TargetSlotHandle);
		if (CompletedDelegate.IsBound())
		{
			CompletedDelegate.Execute();
			// UE::PersistentState::ScheduleAsyncComplete([CompletedDelegate] { CompletedDelegate.Execute(); });
		}
	};

	if (UPersistentStateSettings::Get()->bForceGameThread || UE::PersistentState::GPersistentStateStorage_ForceGameThread)
	{
		TaskBody();
		return UE::Tasks::FTask{};
	}
	
	UE::Tasks::FTask SaveTask = TaskPipe.Launch(UE_SOURCE_LOCATION, MoveTemp(TaskBody), LowLevelTasks::ETaskPriority::High);
	return SaveTask;
}

UE::Tasks::FTask UPersistentStateSlotStorage::LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName, FLoadCompletedDelegate CompletedDelegate)
{
	check(IsInGameThread());

	auto TaskBody = [this, TargetSlotHandle, WorldName, CompletedDelegate]
	{
		auto [GameState, WorldState] = LoadState(TargetSlotHandle, WorldName);
		// @todo: data decompression for GameState/WorldState
		if (CompletedDelegate.IsBound())
		{
			CompletedDelegate.Execute(GameState, WorldState);
			// UE::PersistentState::ScheduleAsyncComplete([WorldState, CompletedDelegate] { CompletedDelegate.Execute(WorldState); });
		}
	};
	
	if (UPersistentStateSettings::Get()->bForceGameThread || UE::PersistentState::GPersistentStateStorage_ForceGameThread)
	{
		TaskBody();
		return UE::Tasks::FTask{};
	}

	UE::Tasks::FTask LoadTask = TaskPipe.Launch(UE_SOURCE_LOCATION, MoveTemp(TaskBody), LowLevelTasks::ETaskPriority::High);
	return LoadTask;
}

void UPersistentStateSlotStorage::UpdateAvailableStateSlots()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	const UPersistentStateSettings* Settings = UPersistentStateSettings::Get();
	
	TArray<FString> SaveGameNamesStr = GetSaveGameNames();
	if (SaveGameNamesStr.IsEmpty())
	{
		// no save files found, reset file data and remove runtime created slots
		for (auto It = StateSlots.CreateIterator(); It; ++It)
		{
			FPersistentStateSlotSharedRef Slot = *It;
			if (Settings->IsDefaultNamedSlot(Slot->GetSlotName()))
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
	
	TArray<FName, TInlineAllocator<8>> SaveGameNames;
	SaveGameNames.Reserve(SaveGameNamesStr.Num());
	for (const FString& SaveGameName: SaveGameNamesStr)
	{
		SaveGameNames.Add(*FPaths::GetBaseFilename(SaveGameName));
	}

	TArray<FString, TInlineAllocator<8>> SaveGameFiles;
	SaveGameFiles.Reserve(SaveGameNames.Num());
	for (const FName& SaveGameName: SaveGameNames)
	{
		SaveGameFiles.Add(Settings->GetSaveGameFilePath(SaveGameName));
	}
	
	TArray<bool, TInlineAllocator<8>> SaveGameNameStatus;
	SaveGameNameStatus.SetNum(SaveGameFiles.Num());

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
			if (Settings->IsDefaultNamedSlot(Slot->GetSlotName()))
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

FPersistentStateSlotHandle UPersistentStateSlotStorage::CreateStateSlot(const FName& SlotName, const FText& Title)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	if (FPersistentStateSlotSharedRef Slot = FindSlot(SlotName); Slot.IsValid())
	{
		ensureAlwaysMsgf(false, TEXT("%s: trying to create slot with name %s that already exists."), *FString(__FUNCTION__), *SlotName.ToString());
		return FPersistentStateSlotHandle{*this, SlotName};
	}

	FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(SlotName, Title);
	CreateSaveGameFile(Slot);
	StateSlots.Add(Slot);
	
	return FPersistentStateSlotHandle{*this, SlotName};
}

void UPersistentStateSlotStorage::GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
{
	OutStates.Reset(StateSlots.Num());
	for (const FPersistentStateSlotSharedRef& Slot: StateSlots)
	{
		if (!bOnDiskOnly || Slot->HasFilePath())
		{
			OutStates.Add(FPersistentStateSlotHandle{*this, Slot->GetSlotName()});
		}
	}
}

FPersistentStateSlotHandle UPersistentStateSlotStorage::GetStateSlotByName(FName SlotName) const
{
	FPersistentStateSlotSharedRef Slot = FindSlot(SlotName);
	if (Slot.IsValid())
	{
		return FPersistentStateSlotHandle{*this, Slot->GetSlotName()};
	}

	return FPersistentStateSlotHandle::InvalidHandle;
}

FName UPersistentStateSlotStorage::GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotSharedRef Slot = FindSlot(SlotHandle.GetSlotName());
	if (!Slot.IsValid())
	{
		return NAME_None;
	}

	return Slot->GetWorldToLoad();
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
	if (UPersistentStateSettings::Get()->IsDefaultNamedSlot(SlotName))
	{
		return true;
	}

	return HasSaveGameFile(StateSlot);
}

void UPersistentStateSlotStorage::RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
{
	const FName SlotName = SlotHandle.GetSlotName();
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotName);
	if (!StateSlot.IsValid())
	{
		return;
	}

	if (const FString& FilePath = StateSlot->GetFilePath(); !FilePath.IsEmpty())
	{
		RemoveSaveGameFile(StateSlot->GetFilePath());
	}

	if (UPersistentStateSettings::Get()->IsDefaultNamedSlot(SlotName))
	{
		StateSlot->ResetFileData();
	}
	else
	{
		StateSlots.RemoveSwap(StateSlot);
	}
}

void UPersistentStateSlotStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
{
	check(GameState.IsValid() && WorldState.IsValid());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	FPersistentStateSlotSharedRef SourceSlot = FindSlot(SourceSlotHandle.GetSlotName());
	check(SourceSlot.IsValid());
	
	FPersistentStateSlotSharedRef TargetSlot = FindSlot(TargetSlotHandle.GetSlotName());
	check(TargetSlot.IsValid());

	CurrentSlotHandle = TargetSlotHandle;
	CurrentGameState = GameState;
	CurrentWorldState = WorldState;

	if (!TargetSlot->HasFilePath())
	{
		check(UPersistentStateSettings::Get()->IsDefaultNamedSlot(TargetSlot->GetSlotName()));
		CreateSaveGameFile(TargetSlot);
	}
	
	TargetSlot->SaveState(
		*SourceSlot, GameState, WorldState,
		[this](const FString& FilePath) { return CreateSaveGameReader(FilePath); },
		[this](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
	);
}

TPair<FGameStateSharedRef, FWorldStateSharedRef> UPersistentStateSlotStorage::LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	FPersistentStateSlotSharedRef TargetStateSlot = FindSlot(TargetSlotHandle.GetSlotName());
	if (!TargetStateSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Target slot %s is no longer valid."), *FString(__FUNCTION__), *TargetSlotHandle.ToString());
		return {};
	}
	
	if (TargetStateSlot->HasFilePath() == false)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Trying to load world state %s from a slot %s that doesn't have associated file path."),
			*FString(__FUNCTION__), *WorldToLoad.ToString(), *TargetSlotHandle.GetSlotName().ToString());
		return {};
	}

	FGameStateSharedRef LoadedGameState = nullptr;
	FWorldStateSharedRef LoadedWorldState = nullptr;

	if (CurrentSlotHandle == TargetSlotHandle && CurrentGameState.IsValid())
	{
		// if we're loading the same slot, reuse previously loaded game state
		LoadedGameState = CurrentGameState;
	}
	else
	{
		LoadedGameState = TargetStateSlot->LoadGameState([this](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
		// keep most recently used world state and slot up to date
		CurrentGameState = LoadedGameState;
	}
	
	if (CurrentSlotHandle == TargetSlotHandle && CurrentWorldState.IsValid() && CurrentWorldState->GetWorld() == WorldToLoad)
	{
		// if we're loading the same world from the same slot, we can reuse previously loaded world state
		LoadedWorldState = CurrentWorldState;
	}
	else if (TargetStateSlot->HasWorldState(WorldToLoad))
	{
		LoadedWorldState = TargetStateSlot->LoadWorldState(WorldToLoad, [this](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
		// keep most recently used world state and slot up to date 
		CurrentWorldState = LoadedWorldState;
		check(CurrentWorldState.IsValid());
	}
	
	// keep most recently used world state and slot up to date 
	CurrentSlotHandle = TargetSlotHandle;
	return {LoadedGameState, LoadedWorldState};
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
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameWriter(const FString& FilePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent | FILEWRITE_EvenIfReadOnly)};
}

TArray<FString> UPersistentStateSlotStorage::GetSaveGameNames() const
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

