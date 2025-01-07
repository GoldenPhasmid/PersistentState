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
	check(NamedSlots.IsEmpty() && RuntimeSlots.IsEmpty());

	UpdateAvailableStateSlots({});
}

void UPersistentStateSlotStorage::Shutdown()
{
	EnsurePipeCompletion();
}

uint32 UPersistentStateSlotStorage::GetAllocatedSize() const
{
	uint32 TotalMemory = 0;
#if STATS
	TotalMemory += GetClass()->GetStructureSize();
	TotalMemory += NamedSlots.GetAllocatedSize();
	TotalMemory += RuntimeSlots.GetAllocatedSize();
	TotalMemory += sizeof(FPersistentStateSlot) * (NamedSlots.Num() + RuntimeSlots.Num());

	for (const FPersistentStateSlotSharedRef& StateSlot: NamedSlots)
	{
		TotalMemory += StateSlot->GetAllocatedSize();
	}

	for (const FPersistentStateSlotSharedRef& StateSlot: RuntimeSlots)
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

void UPersistentStateSlotStorage::EnsurePipeCompletion() const
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

	CurrentSlot = FPersistentStateSlotHandle{*this, TargetSlot->GetSlotName()};
	if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
	{
		CurrentGameState = GameState;
		CurrentWorldState = WorldState;
	}
	
	auto SaveStateTask = [GameState, WorldState, SourceSlot, TargetSlot, CompletedDelegate]
	{
		AsyncSaveState(GameState, WorldState, SourceSlot, TargetSlot);
		if (CompletedDelegate.IsBound())
		{
			AsyncTask(ENamedThreads::Type::GameThread, [CompletedDelegate]
			{
				CompletedDelegate.Execute();
			});
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

	if (TargetSlotHandle != CurrentSlot)
	{
		// reset cached game and world state if slot changes
		CurrentGameState.Reset();
		CurrentWorldState.Reset();
	}
	if (CurrentWorldState.IsValid() && CurrentWorldState->GetWorld() != WorldToLoad)
	{
		// reset world state if loading a different world
		CurrentWorldState.Reset();
	}
	CurrentSlot = TargetSlotHandle;
	
	auto TaskBody = [WeakThis=TWeakObjectPtr<ThisClass>{this}, TargetSlot, WorldToLoad, CachedGameState = CurrentGameState, CachedWorldState = CurrentWorldState, CompletedDelegate]
	{
		auto [GameState, WorldState] = AsyncLoadState(TargetSlot, WorldToLoad, CachedGameState, CachedWorldState);
		AsyncTask(ENamedThreads::Type::GameThread, [WeakThis, TargetSlot, GameState, WorldState, CompletedDelegate]
		{
			check(IsInGameThread());
			if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
			{
				Storage->CompleteLoadState(TargetSlot, GameState, WorldState, CompletedDelegate);
			}
		});
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

void UPersistentStateSlotStorage::CompleteLoadState(FPersistentStateSlotSharedRef TargetSlot, FGameStateSharedRef LoadedGameState, FWorldStateSharedRef LoadedWorldState, FLoadCompletedDelegate CompletedDelegate)
{
	check(IsInGameThread());
	// keep most recently used slot up to date
	CurrentSlot = FPersistentStateSlotHandle{*this, TargetSlot->GetSlotName()};
	if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
	{
		CurrentGameState = LoadedGameState;
		CurrentWorldState = LoadedWorldState;
	}
			
	if (CompletedDelegate.IsBound())
	{
		CompletedDelegate.Execute(LoadedGameState, LoadedWorldState);
	}
}

void UPersistentStateSlotStorage::UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate)
{
	const UPersistentStateSettings* Settings = UPersistentStateSettings::Get();

	const FString Path = Settings->GetSaveGamePath();
	const FString Extension = Settings->GetSaveGameExtension();
	
	TaskPipe.Launch(UE_SOURCE_LOCATION, [WeakThis=TWeakObjectPtr<ThisClass>{this}, Path, Extension, CompletedDelegate, NamedSlots = Settings->DefaultNamedSlots]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentState_UpdateAvailableStateSlots, PersistentStateChannel);
		
		if (!IFileManager::Get().DirectoryExists(*Path))
		{
			IFileManager::Get().MakeDirectory(*Path, true);
		}
		
		TArray<FPersistentStateSlotSharedRef> NewNamedSlots;
		TArray<FPersistentStateSlotSharedRef> NewRuntimeSlots;

		for (const FPersistentSlotEntry& Entry: NamedSlots)
		{
			FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(Entry.SlotName, Entry.Title);
			NewNamedSlots.Add(Slot);
		}
		
		TArray<FString> SaveGameFiles;
		IFileManager::Get().FindFiles(SaveGameFiles, *Path, *Extension);

		if (SaveGameFiles.Num() > 0)
		{
			// make full paths
			for (FString& FileName : SaveGameFiles)
			{
				FileName = FPaths::ConvertRelativePathToFull(Path / FileName);
			}
			
			TArray<FName, TInlineAllocator<8>> SaveGameNames;
			SaveGameNames.Reserve(SaveGameFiles.Num());
			
			for (const FString& SaveGameName: SaveGameFiles)
			{
				SaveGameNames.Add(*FPaths::GetBaseFilename(SaveGameName));
			}

			TArray<bool, TInlineAllocator<8>> SaveGameNameStatus;
			SaveGameNameStatus.SetNum(SaveGameFiles.Num());

			// match state slots with save game files, remove non-persistent slots that doesn't have a valid save file
			for (auto& Slot: NewNamedSlots)
			{
				// slot's file path matched to any file path
				// @note: we do not handle ABA - e.g. old file is replaced with a new file with the same name but different contents
				if (int32 SaveGameIndex = SaveGameNames.IndexOfByKey(Slot->GetSlotName()); SaveGameIndex != INDEX_NONE)
				{
					SaveGameNameStatus[SaveGameIndex] = true;
					
					if (!Slot->IsValidSlot() || Slot->GetFilePath() != SaveGameFiles[SaveGameIndex])
					{
						TUniquePtr<FArchive> ReadArchive = UPersistentStateSlotStorage::CreateSaveGameReader(SaveGameFiles[SaveGameIndex]);
						Slot->TrySetFilePath(*ReadArchive, SaveGameFiles[SaveGameIndex]);
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
				
				TUniquePtr<FArchive> ReadArchive = UPersistentStateSlotStorage::CreateSaveGameReader(SaveGameFiles[Index]);
				FPersistentStateSlot NewSlot{*ReadArchive, SaveGameFiles[Index]};

				if (!NewSlot.IsValidSlot())
				{
					UE_LOG(LogPersistentState, Display, TEXT("%s: Found corrupted save game file %s"), *FString(__FUNCTION__), *SaveGameFiles[Index]);
					continue;
				}
				
				if (const int32 ExistingSlotIndex = NewRuntimeSlots.IndexOfByPredicate([&NewSlot](const FPersistentStateSlotSharedRef& Slot)
				{
					return Slot->GetSlotName() == NewSlot.GetSlotName();
				}); ExistingSlotIndex != INDEX_NONE)
				{
					UE_LOG(LogPersistentState, Error, TEXT("%s: Found collision between named slots. New File [%s], Existing File [%s]. New file is ignored."),
						*FString(__FUNCTION__), *NewSlot.GetFilePath(), *NewNamedSlots[ExistingSlotIndex]->GetFilePath());
					continue;
				}

				// add new shared state slot
				NewRuntimeSlots.Add(MakeShared<FPersistentStateSlot>(NewSlot));
			}
		}
		
		AsyncTask(ENamedThreads::Type::GameThread, [WeakThis, NewNamedSlots, NewRuntimeSlots, CompletedDelegate]
		{
			if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
			{
				Storage->CompleteSlotUpdate(NewNamedSlots, NewRuntimeSlots, CompletedDelegate);
			}
		});
		
	}, LowLevelTasks::ETaskPriority::High);
}

void UPersistentStateSlotStorage::CompleteSlotUpdate(const TArray<FPersistentStateSlotSharedRef>& NewNamedSlots, const TArray<FPersistentStateSlotSharedRef>& NewRuntimeSlots, FSlotUpdateCompletedDelegate CompletedDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	NamedSlots = NewNamedSlots;
	RuntimeSlots = NewRuntimeSlots;
	
	if (!CurrentSlot.IsValid())
	{
		// reset game data for slot that no more exists
		CurrentGameState.Reset();
		CurrentWorldState.Reset();
	}

	if (CompletedDelegate.IsBound())
	{
		TArray<FPersistentStateSlotHandle> OutSlots;
		GetAvailableStateSlots(OutSlots, false);
		
		CompletedDelegate.Execute(OutSlots);
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
	
	RuntimeSlots.Add(Slot);
	return FPersistentStateSlotHandle{*this, SlotName};
}

void UPersistentStateSlotStorage::GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
{
	OutStates.Reset(NamedSlots.Num() + RuntimeSlots.Num());
	for (const FPersistentStateSlotSharedRef& Slot: NamedSlots)
	{
		if (!bOnDiskOnly || Slot->HasFilePath())
		{
			OutStates.Add(FPersistentStateSlotHandle{*this, Slot->GetSlotName()});
		}
	}
	
	for (const FPersistentStateSlotSharedRef& Slot: RuntimeSlots)
	{
		OutStates.Add(FPersistentStateSlotHandle{*this, Slot->GetSlotName()});
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
	bool bNamedSlot = false;
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle, &bNamedSlot);
	if (!StateSlot.IsValid())
	{
		return false;
	}
	
	if (bNamedSlot)
	{
		return true;
	}

	return HasSaveGameFile(StateSlot);
}

void UPersistentStateSlotStorage::RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
{
	bool bNamedSlot = false;
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle, &bNamedSlot);
	
	if (!StateSlot.IsValid())
	{
		return;
	}

	if (CurrentSlot.GetSlotName() == StateSlot->GetSlotName())
	{
		// remove cached game data if slot is being removed
		CurrentGameState.Reset();
		CurrentWorldState.Reset();
	}
	
	if (StateSlot->HasFilePath())
	{
		// make as async task to remove file associated with a slot
		TaskPipe.Launch(UE_SOURCE_LOCATION, [FilePath=StateSlot->GetFilePath()]
		{
			if (!FilePath.IsEmpty())
			{
				RemoveSaveGameFile(FilePath);
			}
		});
	}

	if (bNamedSlot)
	{
		// reset file data for named slots
		StateSlot->ResetFileData();
	}
	else
	{
		// remove runtime slots entirely
		RuntimeSlots.RemoveSwap(StateSlot);
	}
}

void UPersistentStateSlotStorage::AsyncSaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot)
{
	check(GameState.IsValid() && WorldState.IsValid());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	if (!TargetSlot->HasFilePath())
	{
		UPersistentStateSlotStorage::CreateSaveGameFile(TargetSlot);
	}

	// @todo: data compression for GameState/WorldState
	TargetSlot->SaveState(
		*SourceSlot, GameState, WorldState,
		[](const FString& FilePath) { return CreateSaveGameReader(FilePath); },
		[](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
	);
}

TPair<FGameStateSharedRef, FWorldStateSharedRef> UPersistentStateSlotStorage::AsyncLoadState(FPersistentStateSlotSharedRef TargetSlot, FName WorldToLoad, FGameStateSharedRef CachedGameState, FWorldStateSharedRef CachedWorldState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(TargetSlot.IsValid());

	FGameStateSharedRef LoadedGameState = nullptr;
	FWorldStateSharedRef LoadedWorldState = nullptr;
	
	if (CachedGameState.IsValid())
	{
		// if we're loading the same slot, reuse previously loaded game state
		LoadedGameState = CachedGameState;
	}
	else
	{
		LoadedGameState = TargetSlot->LoadGameState([](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
	}
	
	if (CachedWorldState.IsValid() && CachedWorldState->GetWorld() == WorldToLoad)
	{
		// if we're loading the same world from the same slot, we can reuse previously loaded world state
		LoadedWorldState = CachedWorldState;
	}
	else if (TargetSlot->HasWorldState(WorldToLoad))
	{
		LoadedWorldState = TargetSlot->LoadWorldState(WorldToLoad, [](const FString& FilePath) { return CreateSaveGameReader(FilePath); });
	}
	
	return {LoadedGameState, LoadedWorldState};
}

FPersistentStateSlotSharedRef UPersistentStateSlotStorage::FindSlot(const FPersistentStateSlotHandle& SlotHandle, bool* OutNamedSlot) const
{
	return FindSlot(SlotHandle.GetSlotName(), OutNamedSlot);
}

FPersistentStateSlotSharedRef UPersistentStateSlotStorage::FindSlot(FName SlotName, bool* OutNamedSlot) const
{
	bool bIsNamedSlot = false;
	ON_SCOPE_EXIT
	{
		if (OutNamedSlot)
		{
			*OutNamedSlot = bIsNamedSlot;
		}
	};
	
	for (const FPersistentStateSlotSharedRef& Slot: NamedSlots)
	{
		if (Slot->GetSlotName() == SlotName)
		{
			bIsNamedSlot = true;
			return Slot;
		}
	}

	for (const FPersistentStateSlotSharedRef& Slot: RuntimeSlots)
	{
		if (Slot->GetSlotName() == SlotName)
		{
			return Slot;
		}
	}

	return {};
}

bool UPersistentStateSlotStorage::HasSaveGameFile(const FPersistentStateSlotSharedRef& Slot)
{
	return Slot->HasFilePath() && IFileManager::Get().FileExists(*Slot->GetFilePath());
}

void UPersistentStateSlotStorage::CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot)
{
	// create file and associate it with a slot
	auto Settings = UPersistentStateSettings::Get();
	const FName SlotName = Slot->GetSlotName();
	
	const FString FilePath = Settings->GetSaveGameFilePath(SlotName);
	Slot->SetFilePath(FilePath);
	TUniquePtr<FArchive> Archive = CreateSaveGameWriter(FilePath);

	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame file is created: %s"), *FilePath);
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameReader(const FString& FilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame reader: %s"), *FilePath);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateSaveGameWriter(const FString& FilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame writer: %s"), *FilePath);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent | FILEWRITE_EvenIfReadOnly)};
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

