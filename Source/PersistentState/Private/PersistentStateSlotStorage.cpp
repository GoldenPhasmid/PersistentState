#include "PersistentStateSlotStorage.h"

#include "ImageUtils.h"
#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"

FString FPersistentStateSlotDesc::ToString() const
{
	return FString::Printf(TEXT("Name: %s, Title: %s, FilePath: %s, Saved World: %s"),
		*SlotName.ToString(), *SlotTitle.ToString(), *FPaths::ConvertRelativePathToFull(FilePath), *LastSavedWorld.ToString());
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

void UPersistentStateSlotStorage::EnsureTaskCompletion() const
{
	check(IsInGameThread());
	if (TaskPipe.HasWork())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSlotStorage_WaitPipeComplete, PersistentStateChannel);
		UE::PersistentState::WaitForPipe(const_cast<UE::Tasks::FPipe&>(TaskPipe));
	}
}

UE::Tasks::FTask UPersistentStateSlotStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
{
	check(IsInGameThread());
	check(GameState.IsValid() && WorldState.IsValid());

	FPersistentStateSlotSharedRef SourceSlot = FindSlot(SourceSlotHandle);
	if (!SourceSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Source slot %s is no longer valid."), *FString(__FUNCTION__), *SourceSlotHandle.ToString());
        return {};
	}
	
	FPersistentStateSlotSharedRef TargetSlot = FindSlot(TargetSlotHandle);
	if (!TargetSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Target slot %s is no longer valid."), *FString(__FUNCTION__), *TargetSlotHandle.ToString());
		return {};
	}

	// handle screenshot capture
	auto Settings = UPersistentStateSettings::Get();
	if (Settings->bCaptureScreenshot)
	{
		SlotsForScreenshotCapture.AddUnique(TargetSlotHandle);
		if (!CaptureScreenshotHandle.IsValid())
		{
			GIsHighResScreenshot = true;
			GScreenshotResolutionX = Settings->ScreenshotResolution.X;
			GScreenshotResolutionY = Settings->ScreenshotResolution.Y;
			FScreenshotRequest::RequestScreenshot(Settings->bCaptureUI);
			CaptureScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ThisClass::HandleScreenshotCapture);
		}
	}
	
	auto SaveStateTask = [this, GameState, WorldState, SourceSlot, TargetSlot, CompletedDelegate]
	{
		SaveState(GameState, WorldState, SourceSlot, TargetSlot);
		if (CompletedDelegate.IsBound())
		{
			UE::PersistentState::ScheduleGameThreadCallback(FSimpleDelegateGraphTask::FDelegate::CreateLambda([CompletedDelegate]
			{
				CompletedDelegate.Execute();
			}));
		}
	};

	if (Settings->UseGameThread())
	{
		// run directly on game thread instead of waiting for lower priority thread
		SaveStateTask();
		return UE::Tasks::FTask{};
	}
	
	UE::Tasks::FTask SaveTask = TaskPipe.Launch(UE_SOURCE_LOCATION, MoveTemp(SaveStateTask), LowLevelTasks::ETaskPriority::High);
	return SaveTask;
}

UE::Tasks::FTask UPersistentStateSlotStorage::LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, FLoadCompletedDelegate CompletedDelegate)
{
	check(IsInGameThread());

	FPersistentStateSlotSharedRef TargetSlot = FindSlot(TargetSlotHandle);
	if (!TargetSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Target slot %s is no longer valid."), *FString(__FUNCTION__), *TargetSlotHandle.ToString());
		return {};
	}

	if (TargetSlot->HasFilePath() == false)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Trying to load world state %s from a slot %s that doesn't have associated file path."),
			*FString(__FUNCTION__), *WorldToLoad.ToString(), *TargetSlotHandle.GetSlotName().ToString());
		return {};
	}
	
	auto TaskBody = [this, TargetSlot, WorldToLoad, CompletedDelegate]
	{
		auto [GameState, WorldState] = LoadState(TargetSlot, WorldToLoad);
		// @todo: data decompression for GameState/WorldState
		if (CompletedDelegate.IsBound())
		{
			UE::PersistentState::ScheduleGameThreadCallback(FSimpleDelegateGraphTask::FDelegate::CreateLambda([CompletedDelegate, GameState, WorldState]
			{
				CompletedDelegate.Execute(GameState, WorldState);
			}));
			if (IsInGameThread())
			{
				CompletedDelegate.Execute(GameState, WorldState);
			}
		}
	};
	
	if (UPersistentStateSettings::Get()->UseGameThread())
	{
		// run directly on game thread instead of waiting for lower priority thread
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
		
		// slot's file path matched to any file path
		// @note: we do not handle ABA - e.g. old file is replaced with a new file with the same name but different contents
		if (int32 SaveGameIndex = SaveGameNames.IndexOfByKey(Slot->GetSlotName()); SaveGameIndex != INDEX_NONE)
		{
			SaveGameNameStatus[SaveGameIndex] = true;
			if (!Slot->IsValidSlot() || Slot->GetFilePath() != SaveGameFiles[SaveGameIndex])
			{
				const FString& FilePath = SaveGameFiles[SaveGameIndex];
				
				TUniquePtr<FArchive> ReadArchive = CreateSaveGameReader(FilePath);
				Slot->TrySetFilePath(*ReadArchive, SaveGameFiles[SaveGameIndex]);
			}
		}
		else
		{
			const bool bSameSlot = CurrentSlot.IsValid() && CurrentSlot.Pin() == Slot;
			// if we failed to match currently cached slot, reset cached game/world data as well
			if (bSameSlot)
			{
				CurrentGameState.Reset();
				CurrentWorldState.Reset();
			}
			
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
		FPersistentStateSlot NewSlot{*ReadArchive, SaveGameFiles[Index]};

		if (!NewSlot.IsValidSlot())
		{
			UE_LOG(LogPersistentState, Display, TEXT("%s: Found corrupted save game file %s"), *FString(__FUNCTION__), *SaveGameFiles[Index]);
			continue;
		}
		
		if (const int32 ExistingSlotIndex = StateSlots.IndexOfByPredicate([&NewSlot](const FPersistentStateSlotSharedRef& Slot)
		{
			return Slot->GetSlotName() == NewSlot.GetSlotName();
		}); ExistingSlotIndex != INDEX_NONE)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: Found collision between named slots. New File [%s], Existing File [%s]. New file is ignored."),
				*FString(__FUNCTION__), *NewSlot.GetFilePath(), *StateSlots[ExistingSlotIndex]->GetFilePath());
			continue;
		}
		
		// add new shared state slot
		StateSlots.Add(MakeShared<FPersistentStateSlot>(NewSlot));
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

FPersistentStateSlotDesc UPersistentStateSlotStorage::GetStateSlotDesc( const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotSharedRef Slot = FindSlot(SlotHandle.GetSlotName());
	if (!Slot.IsValid())
	{
		return {};
	}

	return FPersistentStateSlotDesc{*Slot};
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

	return Slot->GetLastSavedWorld();
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

	if (UPersistentStateSettings::Get()->IsDefaultNamedSlot(SlotName))
	{
		StateSlot->ResetFileData();
	}
	else
	{
		StateSlots.RemoveSwap(StateSlot);
	}

	TaskPipe.Launch(UE_SOURCE_LOCATION, [this, StateSlot]
	{
		if (const FString& FilePath = StateSlot->GetFilePath(); !FilePath.IsEmpty())
		{
			RemoveSaveGameFile(StateSlot->GetFilePath());
		}
	});
}

void UPersistentStateSlotStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot)
{
	check(GameState.IsValid() && WorldState.IsValid());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	CurrentSlot = TargetSlot;
	if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
	{
		CurrentGameState = GameState;
		CurrentWorldState = WorldState;
	}
	
	if (!TargetSlot->HasFilePath())
	{
		check(UPersistentStateSettings::IsDefaultNamedSlot(TargetSlot->GetSlotName()));
		CreateSaveGameFile(TargetSlot);
	}

	// @todo: data compression for GameState/WorldState
	TargetSlot->SaveState(
		*SourceSlot, GameState, WorldState,
		[this](const FString& FilePath) { return CreateSaveGameReader(FilePath); },
		[this](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
	);
}

TPair<FGameStateSharedRef, FWorldStateSharedRef> UPersistentStateSlotStorage::LoadState(FPersistentStateSlotSharedRef TargetSlot, FName WorldToLoad)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(TargetSlot.IsValid());

	FGameStateSharedRef LoadedGameState = nullptr;
	FWorldStateSharedRef LoadedWorldState = nullptr;

	const bool bCurrentSlot = CurrentSlot.IsValid() && CurrentSlot.Pin() == TargetSlot;
	if (bCurrentSlot && CurrentGameState.IsValid())
	{
		// if we're loading the same slot, reuse previously loaded game state
		LoadedGameState = CurrentGameState;
	}
	else
	{
		LoadedGameState = TargetSlot->LoadGameState([this](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
		if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
		{
			// keep most recently used world state and slot up to date
			CurrentGameState = LoadedGameState;
			check(CurrentGameState.IsValid());
		}
	}
	
	if (bCurrentSlot && CurrentWorldState.IsValid() && CurrentWorldState->GetWorld() == WorldToLoad)
	{
		// if we're loading the same world from the same slot, we can reuse previously loaded world state
		LoadedWorldState = CurrentWorldState;
	}
	else if (TargetSlot->HasWorldState(WorldToLoad))
	{
		LoadedWorldState = TargetSlot->LoadWorldState(WorldToLoad, [this](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
		if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
		{
			// keep most recently used world state and slot up to date 
			CurrentWorldState = LoadedWorldState;
			check(CurrentWorldState.IsValid());
		}
	}
	
	// keep most recently used slot up to date 
	CurrentSlot = TargetSlot;
	return {LoadedGameState, LoadedWorldState};
}

FPersistentStateSlotSharedRef UPersistentStateSlotStorage::FindSlot(const FPersistentStateSlotHandle& SlotHandle) const
{
	return FindSlot(SlotHandle.GetSlotName());
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

	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame file is created: %s"), *FilePath);
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameReader(const FString& FilePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame reader: %s"), *FilePath);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameWriter(const FString& FilePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame writer: %s"), *FilePath);
	
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
	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame file removed: %s"), *FilePath);
	IFileManager::Get().Delete(*FilePath, true, false, true);
}

void UPersistentStateSlotStorage::HandleScreenshotCapture(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentState_HandleScreenshot, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("HandleScreenshot: Width %d, Height %d"), Width, Height);
	check(IsInGameThread());
	
	struct FScreenshotData
	{
		FIntPoint Size{};
		TArray<FColor> Bitmap;
		TArray64<uint8> CompressedData;
	};
	
	TSharedPtr<FScreenshotData, ESPMode::ThreadSafe> Image = MakeShared<FScreenshotData, ESPMode::ThreadSafe>();
	Image->Size = FIntPoint{Width, Height};
	Image->Bitmap = Bitmap;

	auto Settings = UPersistentStateSettings::Get();
	// compress color data task
	UE::Tasks::FTask CompressTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Image, Extension = Settings->ScreenshotExtension]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentState_CompressImage, PersistentStateChannel);
		for (FColor& Color : Image->Bitmap)
		{
			Color.A = 255;
		}
		
		FImageView ImageView{Image->Bitmap.GetData(), Image->Size.X, Image->Size.Y};
		const bool bCompressResult = FImageUtils::CompressImage(Image->CompressedData, *Extension, ImageView, 0);
		check(bCompressResult);
	}, LowLevelTasks::ETaskPriority::BackgroundNormal);
	
	for (const FPersistentStateSlotHandle& Slot: SlotsForScreenshotCapture)
	{
		if (!Slot.IsValid())
		{
			continue;
		}

		const FString TaskName = *FString::Printf(TEXT("%s_%s"), UE_SOURCE_LOCATION, *Slot.GetSlotName().ToString());
		const FString Filename = Settings->GetScreenshotFilePath(Slot.GetSlotName());
		UE::Tasks::FTask Task = UE::Tasks::Launch(*TaskName, [Image, Filename]
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentState_SaveImage, PersistentStateChannel);
			const bool bSaveResult = FFileHelper::SaveArrayToFile(Image->CompressedData, *Filename);
			check(bSaveResult);
		}, UE::Tasks::Prerequisites(CompressTask), LowLevelTasks::ETaskPriority::BackgroundNormal);
	}

	// clear screenshot requests and callbacks
	SlotsForScreenshotCapture.Reset();
	UGameViewportClient::OnViewportRendered().Remove(CaptureScreenshotHandle);
}

