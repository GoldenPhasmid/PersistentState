#include "PersistentStateSlotStorage.h"

#include "ImageUtils.h"
#include "PersistentStateModule.h"
#include "PersistentStateSerialization.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "Engine/Texture2DDynamic.h"

FString FPersistentStateSlotDesc::ToString() const
{
	return FString::Printf(TEXT("Name: %s, Title: %s, FilePath: %s, Saved World: %s"),
		*SlotName.ToString(), *SlotTitle.ToString(), *FPaths::ConvertRelativePathToFull(FilePath), *LastSavedWorld.ToString());
}

UPersistentStateSlotStorage::UPersistentStateSlotStorage(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	
}
UPersistentStateSlotStorage::UPersistentStateSlotStorage(FVTableHelper& Helper)
	: Super(Helper)
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
	EnsureTaskCompletion();
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

void UPersistentStateSlotStorage::EnsureTaskCompletion() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(IsInGameThread());

	// wait for ALL tasks to complete. last launched task requires all previous tasks to complete
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(LastQueuedEvent, ENamedThreads::GameThread);
}

FGraphEventArray UPersistentStateSlotStorage::GetPrerequisites() const
{
	FGraphEventArray Prerequisites;
	if (LastQueuedEvent.IsValid())
	{
		Prerequisites.Add(LastQueuedEvent);
	}

	return Prerequisites;
}

FGraphEventRef UPersistentStateSlotStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
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
	QueueScreenshotCapture(TargetSlotHandle);

	CurrentSlot = FPersistentStateSlotHandle{*this, TargetSlot->GetSlotName()};
	if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
	{
		CurrentGameState = GameState;
		CurrentWorldState = WorldState;
	}

	FGraphEventArray Prerequisites = GetPrerequisites();

	const FString FilePath = UPersistentStateSettings::Get()->GetSaveGameFilePath(TargetSlot->GetSlotName());
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([GameState, WorldState, SourceSlot, TargetSlot, FilePath]
	{
		// @note: @SourceSlot is never modified for save operation!
		// @todo: read and write to @TargetSlot are not synchronized. If save operation is in progress and @TargetSlot contents are being updated,
		// GetStateSlotDesc may return corrupted state in-between
		AsyncSaveState(GameState, WorldState, SourceSlot, TargetSlot, FilePath);
	}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	
	if (CompletedDelegate.IsBound())
	{
		LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([CompletedDelegate]
		{
			CompletedDelegate.Execute();
		}, TStatId{}, LastQueuedEvent, ENamedThreads::GameThread);
	}

	if (UPersistentStateSettings::Get()->UseGameThread())
	{
		EnsureTaskCompletion();
	}

	return LastQueuedEvent;
}

FGraphEventRef UPersistentStateSlotStorage::LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, FLoadCompletedDelegate CompletedDelegate)
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

	struct FLoadStateTaskData
	{
		FGameStateSharedRef GameState;
		FWorldStateSharedRef WorldState;
	};
	TSharedPtr<FLoadStateTaskData, ESPMode::ThreadSafe> TaskData = MakeShared<FLoadStateTaskData>();

	FGraphEventArray Prerequisites = GetPrerequisites();
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData, TargetSlot, WorldToLoad, CachedGameState = CurrentGameState, CachedWorldState = CurrentWorldState]
	{
		auto [GameState, WorldState] = AsyncLoadState(TargetSlot, WorldToLoad, CachedGameState, CachedWorldState);
		TaskData->GameState = GameState;
		TaskData->WorldState = WorldState;
	}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis=TWeakObjectPtr<ThisClass>{this}, TaskData, TargetSlot, CompletedDelegate]
	{
		check(IsInGameThread());
		if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
		{
			Storage->CompleteLoadState(TargetSlot, TaskData->GameState, TaskData->WorldState, CompletedDelegate);
		}
	}, TStatId{}, LastQueuedEvent, ENamedThreads::GameThread);
	
	if (UPersistentStateSettings::Get()->UseGameThread())
	{
		// run directly on game thread instead of waiting for lower priority thread
		EnsureTaskCompletion();
	}

	return LastQueuedEvent;
}

void UPersistentStateSlotStorage::SaveStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle)
{
	check(IsInGameThread());
	QueueScreenshotCapture(TargetSlotHandle);
}

bool UPersistentStateSlotStorage::HasScreenshotForStateSlot(const FPersistentStateSlotHandle& TargetSlotHandle)
{
	check(IsInGameThread());

	FPersistentStateSlotSharedRef TargetSlot = FindSlot(TargetSlotHandle);
	if (!TargetSlot.IsValid())
	{
		// slot doesn't exist
		return false;
	}
	
	const FString FilePath = UPersistentStateSettings::Get()->GetScreenshotFilePath(TargetSlot->GetSlotName());
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return false;
	}

	return true;
}

bool UPersistentStateSlotStorage::LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, TFunction<void(UTexture2DDynamic*)> Callback)
{
	const FString FilePath = UPersistentStateSettings::Get()->GetScreenshotFilePath(TargetSlotHandle.GetSlotName());
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return false;
	}

	struct FLoadScreenshotTaskData
	{
		FImage Image;
		UTexture2DDynamic* Texture;
		FTexture2DDynamicResource* TextureResource_GameThread = nullptr;
	};
	
	FFunctionGraphTask::CreateAndDispatchWhenReady([FilePath, Callback]
	{
		TSharedPtr<FLoadScreenshotTaskData, ESPMode::ThreadSafe> TaskData = MakeShared<FLoadScreenshotTaskData>();
		const bool bResult = UE::PersistentState::LoadScreenshot(FilePath, TaskData->Image);
		if (bResult)
		{
			check(TaskData->Image.GetWidth() > 0 && TaskData->Image.GetHeight() > 0);
			FGraphEventRef CreateTextureEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData]
			{
				check(IsInGameThread());
				
				TaskData->Texture = UTexture2DDynamic::Create(TaskData->Image.GetWidth(), TaskData->Image.GetHeight());
				TaskData->Texture->AddToRoot();
				TaskData->TextureResource_GameThread = static_cast<FTexture2DDynamicResource*>(TaskData->Texture->GetResource());
			}, TStatId{}, nullptr, ENamedThreads::GameThread);

			FGraphEventRef WriteTextureEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData]
			{
				check(IsInRenderingThread());
				TaskData->TextureResource_GameThread->WriteRawToTexture_RenderThread(TaskData->Image.RawData);
			}, TStatId{}, CreateTextureEvent, ENamedThreads::ActualRenderingThread);

			FGraphEventRef CompleteEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData, Callback]
			{
				check(IsInGameThread());
				check(TaskData->Texture != nullptr);
				
				TaskData->Texture->RemoveFromRoot();
				Callback(TaskData->Texture);
			}, TStatId{}, CompleteEvent, ENamedThreads::GameThread);
		}
		else
		{
			FGraphEventRef LoadFailedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([Callback]
			{
				Callback(nullptr);
			}, TStatId{}, nullptr, ENamedThreads::GameThread);
		}

	}, TStatId{}, nullptr, ENamedThreads::AnyNormalThreadHiPriTask);
	
	return true;
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

	struct FUpdateSlotTaskData
	{
		TArray<FPersistentStateSlotSharedRef> NamedSlots;
		TArray<FPersistentStateSlotSharedRef> RuntimeSlots;
	};
	TSharedPtr<FUpdateSlotTaskData, ESPMode::ThreadSafe> TaskData = MakeShared<FUpdateSlotTaskData>();

	FGraphEventArray Prerequisites = GetPrerequisites();
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData, Path, Extension, NamedSlots = Settings->DefaultNamedSlots]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentState_UpdateAvailableStateSlots, PersistentStateChannel);
		
		if (!IFileManager::Get().DirectoryExists(*Path))
		{
			IFileManager::Get().MakeDirectory(*Path, true);
		}
		
		for (const FPersistentSlotEntry& Entry: NamedSlots)
		{
			FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(Entry.SlotName, Entry.Title);
			TaskData->NamedSlots.Add(Slot);
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
			for (auto& Slot: TaskData->NamedSlots)
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
				
				if (const int32 ExistingSlotIndex = TaskData->RuntimeSlots.IndexOfByPredicate([&NewSlot](const FPersistentStateSlotSharedRef& Slot)
				{
					return Slot->GetSlotName() == NewSlot.GetSlotName();
				}); ExistingSlotIndex != INDEX_NONE)
				{
					UE_LOG(LogPersistentState, Error, TEXT("%s: Found collision between named slots. New File [%s], Existing File [%s]. New file is ignored."),
						*FString(__FUNCTION__), *NewSlot.GetFilePath(), *TaskData->NamedSlots[ExistingSlotIndex]->GetFilePath());
					continue;
				}

				// add new shared state slot
				TaskData->RuntimeSlots.Add(MakeShared<FPersistentStateSlot>(NewSlot));
			}
		}
	}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis=TWeakObjectPtr<ThisClass>{this}, TaskData, CompletedDelegate]
	{
		if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
		{
			Storage->CompleteSlotUpdate(TaskData->NamedSlots, TaskData->RuntimeSlots, CompletedDelegate);
		}
	}, TStatId{}, LastQueuedEvent, ENamedThreads::GameThread);

	if (UPersistentStateSettings::Get()->UseGameThread())
	{
		EnsureTaskCompletion();
	}
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
		UE_LOG(LogPersistentState, Error, TEXT("%s: trying to create slot with name %s that already exists."), *FString(__FUNCTION__), *SlotName.ToString());
		return FPersistentStateSlotHandle{*this, SlotName};
	}

	FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(SlotName, Title);
	CreateSaveGameFile(Slot, UPersistentStateSettings::Get()->GetSaveGameFilePath(Slot->GetSlotName()));
	
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

bool UPersistentStateSlotStorage::CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const
{
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle.GetSlotName());
	if (!StateSlot.IsValid())
	{
		return false;
	}

	if (!HasSaveGameFile(StateSlot))
	{
		return false;
	}

	return World == NAME_None || StateSlot->HasWorldState(World);
}

bool UPersistentStateSlotStorage::CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const
{
	bool bNamedSlot = false;
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle, &bNamedSlot);
	if (!StateSlot.IsValid())
	{
		return false;
	}

	// any world can be saved to any slot by default
	return bNamedSlot || HasSaveGameFile(StateSlot);
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
		// async remove file associated with a slot
		FGraphEventArray Prerequisites = GetPrerequisites();
		LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([FilePath=StateSlot->GetFilePath()]
		{
			if (!FilePath.IsEmpty())
			{
				RemoveSaveGameFile(FilePath);
			}
		}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
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

void UPersistentStateSlotStorage::AsyncSaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState,
                                                 FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot, const FString& FilePath)
{
	check(GameState.IsValid() && WorldState.IsValid());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	FPersistentStateSlotSharedRef Slot = TargetSlot;
	if (FPersistentStateFormatter::IsTextBased())
	{
		// create a temporary proxy slot that saves to a file with a different extension
		Slot = MakeShared<FPersistentStateSlot>(Slot->GetSlotName(), Slot->GetSlotTitle());
		
		const FString DebugFilePath = FPaths::ChangeExtension(FilePath, FPersistentStateFormatter::GetExtension());
		CreateSaveGameFile(Slot, DebugFilePath);

		Slot->SaveStateDirect(
			GameState, WorldState,
			[](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
		);
	}
	else
	{
		if (!Slot->HasFilePath())
		{
			CreateSaveGameFile(Slot, FilePath);
		}

		// @todo: data compression for GameState/WorldState
		Slot->SaveState(
			*SourceSlot, GameState, WorldState,
			[](const FString& FilePath) { return CreateSaveGameReader(FilePath); },
			[](const FString& FilePath) { return CreateSaveGameWriter(FilePath); }
		);
	}
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
		// @todo: opening a reader may fail if file was deleted
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

void UPersistentStateSlotStorage::CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot, const FString& FilePath)
{
	check(Slot.IsValid() && !Slot->HasFilePath());
	// create file and associate it with a slot
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

void UPersistentStateSlotStorage::QueueScreenshotCapture(const FPersistentStateSlotHandle& Slot)
{
	auto Settings = UPersistentStateSettings::Get();
	if (Settings->bCaptureScreenshot && !SlotsForScreenshotCapture.Contains(Slot))
	{
		SlotsForScreenshotCapture.Add(Slot);
		if (!CaptureScreenshotHandle.IsValid())
		{
			// multiple screenshots can be captured in one frame for different slots. We only have to subscribe once
			GIsHighResScreenshot = true;
			GScreenshotResolutionX = Settings->ScreenshotResolution.X;
			GScreenshotResolutionY = Settings->ScreenshotResolution.Y;
			FScreenshotRequest::RequestScreenshot(Settings->bCaptureUI);
			CaptureScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ThisClass::HandleScreenshotCapture);
		}
	}
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

	// for each state slot that requires screenshot capture, schedule async task after compress task has been finished
	for (const FPersistentStateSlotHandle& Slot: SlotsForScreenshotCapture)
	{
		if (!Slot.IsValid())
		{
			continue;
		}

		const FString TaskName = *FString::Printf(TEXT("%s_%s"), UE_SOURCE_LOCATION, *Slot.GetSlotName().ToString());
		const FString Filename = Settings->GetScreenshotFilePath(Slot.GetSlotName());
		
		UE::Tasks::Launch(*TaskName, [Image, Filename]
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

