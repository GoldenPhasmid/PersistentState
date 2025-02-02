#include "PersistentStateSlotStorage.h"

#include "ImageUtils.h"
#include "PersistentStateModule.h"
#include "PersistentStateSerialization.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSlotDescriptor.h"
#include "PersistentStateStatics.h"
#include "Engine/Texture2DDynamic.h"

class FUpdateAvailableSlotsAsyncTask: public TSharedFromThis<FUpdateAvailableSlotsAsyncTask>
{
public:

	FString Path;
	FString Extension;
	TSubclassOf<UPersistentStateSlotDescriptor> DefaultDescriptor;
	TArray<FPersistentStateSlotSharedRef> NamedSlots;
	TArray<FPersistentStateSlotSharedRef> RuntimeSlots;

	void Run()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FUpdateAvailableSlotsTask_Run, PersistentStateChannel);
		
		if (!IFileManager::Get().DirectoryExists(*Path))
		{
			IFileManager::Get().MakeDirectory(*Path, true);
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
			for (auto& Slot: NamedSlots)
			{
				// slot's file path matched to any file path
				// @note: we do not handle ABA - e.g. old file is replaced with a new file with the same name but different contents
				if (int32 SaveGameIndex = SaveGameNames.IndexOfByKey(Slot->GetSlotName()); SaveGameIndex != INDEX_NONE)
				{
					SaveGameNameStatus[SaveGameIndex] = true;
					
					if (!Slot->IsValidSlot() || Slot->GetFilePath() != SaveGameFiles[SaveGameIndex])
					{
						TUniquePtr<FArchive> ReadArchive = UPersistentStateSlotStorage::CreateStateSlotReader(SaveGameFiles[SaveGameIndex]);
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
				
				TUniquePtr<FArchive> ReadArchive = UPersistentStateSlotStorage::CreateStateSlotReader(SaveGameFiles[Index]);
				FPersistentStateSlot NewSlot{*ReadArchive, SaveGameFiles[Index]};

				if (!NewSlot.IsValidSlot())
				{
					UE_LOG(LogPersistentState, Display, TEXT("%s: Found corrupted save game file %s"), *FString(__FUNCTION__), *SaveGameFiles[Index]);
					continue;
				}
				
				if (const int32 ExistingSlotIndex = RuntimeSlots.IndexOfByPredicate([&NewSlot](const FPersistentStateSlotSharedRef& Slot)
				{
					return Slot->GetSlotName() == NewSlot.GetSlotName();
				}); ExistingSlotIndex != INDEX_NONE)
				{
					UE_LOG(LogPersistentState, Error, TEXT("%s: Found collision between named slots. New File [%s], Existing File [%s]. New file is ignored."),
						*FString(__FUNCTION__), *NewSlot.GetFilePath(), *NamedSlots[ExistingSlotIndex]->GetFilePath());
					continue;
				}

				// add new shared state slot
				RuntimeSlots.Add(MakeShared<FPersistentStateSlot>(NewSlot));
			}
		}
	}
};

class FLoadStateAsyncTask: public TSharedFromThis<FLoadStateAsyncTask>
{
public:
	FLoadStateAsyncTask(const FPersistentStateSlotSharedRef& InTargetSlot, FGameStateSharedRef CurrentGameState, FWorldStateSharedRef CurrentWorldState, FName InWorldToLoad)
		: TargetSlot(InTargetSlot)
		, GameState(CurrentGameState)
		, WorldState(CurrentWorldState)
		, WorldToLoad(InWorldToLoad)
	{
		bLoadGameState	= !GameState.IsValid();
		bLoadWorldState = !WorldState.IsValid() || WorldState->Header.GetWorld() != WorldToLoad;
	}

	void Run()
	{
		check(TargetSlot.IsValid());
		if (bLoadGameState)
		{
			GameState = TargetSlot->LoadGameState([](const FString& FilePath) { return UPersistentStateSlotStorage::CreateStateSlotReader(FilePath); });
		}
		if (bLoadWorldState && TargetSlot->HasWorldState(WorldToLoad))
		{
			// @todo: opening a reader may fail if file was deleted
			WorldState = TargetSlot->LoadWorldState(WorldToLoad, [](const FString& FilePath) { return UPersistentStateSlotStorage::CreateStateSlotReader(FilePath); });
		}
	}
	
	FPersistentStateSlotSharedRef TargetSlot;
	FGameStateSharedRef GameState;
	FWorldStateSharedRef WorldState;
	FName WorldToLoad = NAME_None;
	bool bLoadGameState = false;
	bool bLoadWorldState = false;
};

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

	DefaultDescriptor = UPersistentStateSettings::Get()->DefaultSlotDescriptor;
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

void UPersistentStateSlotStorage::WaitUntilTasksComplete() const
{
	EnsureTaskCompletion();
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
	if (!GameState.IsValid() && !WorldState.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: both GameState and WorldState are invalid for %s: slot save request call."), *FString(__FUNCTION__), *TargetSlotHandle.ToString());
		return {};
	}
	
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
	
	CurrentSlot = TargetSlotHandle;
	if (UPersistentStateSettings::Get()->ShouldCacheSlotState())
	{
		CurrentGameState = GameState;
		CurrentWorldState = WorldState;
	}

	// create save request with descriptor data
	FPersistentStateSlotSaveRequest Request = FPersistentStateSlot::CreateSaveRequest(GetWorld(), *TargetSlot, TargetSlotHandle, GameState, WorldState);
	
	const FGraphEventArray Prerequisites = GetPrerequisites();
	const FString FilePath = UPersistentStateSettings::Get()->GetSaveGameFilePath(TargetSlot->GetSlotName());
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([Request, SourceSlot, TargetSlot, FilePath, Descriptor=DefaultDescriptor]
	{
		// @note: @SourceSlot is never modified for save operation!
		// @todo: read and write to @TargetSlot are not synchronized. If save operation is in progress and @TargetSlot contents are being updated,
		// descriptor may be corrupted if created during save op
		AsyncSaveState(Request, SourceSlot, TargetSlot, FilePath, Descriptor);
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

	if (!TargetSlot->HasWorldState(WorldToLoad))
	{
		UE_LOG(LogPersistentState, Log, TEXT("%s: Failed to find world state for world %s, state slot %s"),
			*FString(__FUNCTION__), *WorldToLoad.ToString(), *TargetSlotHandle.GetSlotName().ToString());
		return {};
	}

	if (TargetSlotHandle != CurrentSlot)
	{
		// reset cached game and world state if slot changes
		CurrentGameState.Reset();
		CurrentWorldState.Reset();
	}
	if (CurrentWorldState.IsValid() && CurrentWorldState->Header.GetWorld() != WorldToLoad)
	{
		// reset world state if loading a different world
		CurrentWorldState.Reset();
	}
	CurrentSlot = TargetSlotHandle;
	
	TSharedPtr<FLoadStateAsyncTask, ESPMode::ThreadSafe> Task = MakeShared<FLoadStateAsyncTask>(TargetSlot, CurrentGameState, CurrentWorldState, WorldToLoad);
	FGraphEventArray Prerequisites = GetPrerequisites();
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([Task]
	{
		check(Task.IsValid());
		Task->Run();
	}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis=TWeakObjectPtr<ThisClass>{this}, Task, CompletedDelegate]
	{
		check(IsInGameThread());
		if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
		{
			Storage->CompleteLoadState_GameThread(Task->TargetSlot, Task->GameState, Task->WorldState, CompletedDelegate);
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
	
	return HasStateSlotScreenshotFile(TargetSlot);
}

bool UPersistentStateSlotStorage::LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, FLoadScreenshotCompletedDelegate CompletedDelegate)
{
	if (!CompletedDelegate.IsBound())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: CompletedDelegate is not bound for %s screenshot request."), *FString(__FUNCTION__), *TargetSlotHandle.ToString());
		return false;
	}
	
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

	// screenshot task doesn't have any prerequisites
	FFunctionGraphTask::CreateAndDispatchWhenReady([FilePath, CompletedDelegate]
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
				// add texture to root so it doesn't get GC'd while we're waiting for RT to initialize texture with data
				TaskData->Texture->AddToRoot();
				TaskData->TextureResource_GameThread = static_cast<FTexture2DDynamicResource*>(TaskData->Texture->GetResource());
			}, TStatId{}, nullptr, ENamedThreads::GameThread);

			FGraphEventRef WriteTextureEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData]
			{
				check(IsInRenderingThread());
				TaskData->TextureResource_GameThread->WriteRawToTexture_RenderThread(TaskData->Image.RawData);
			}, TStatId{}, CreateTextureEvent, ENamedThreads::ActualRenderingThread);

			FGraphEventRef CompleteEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([TaskData, CompletedDelegate]
			{
				check(IsInGameThread());
				check(TaskData->Texture != nullptr);

				// remove from root, it is not a caller responsibility to keep texture alive
				TaskData->Texture->RemoveFromRoot();
				CompletedDelegate.ExecuteIfBound(TaskData->Texture);
			}, TStatId{}, CompleteEvent, ENamedThreads::GameThread);
		}
		else
		{
			FGraphEventRef LoadFailedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([CompletedDelegate]
			{
				CompletedDelegate.ExecuteIfBound(nullptr);
			}, TStatId{}, nullptr, ENamedThreads::GameThread);
		}

	}, TStatId{}, nullptr, ENamedThreads::AnyNormalThreadHiPriTask);
	
	return true;
}

void UPersistentStateSlotStorage::CompleteLoadState_GameThread(FPersistentStateSlotSharedRef TargetSlot, FGameStateSharedRef LoadedGameState, FWorldStateSharedRef LoadedWorldState, FLoadCompletedDelegate CompletedDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
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

FGraphEventRef UPersistentStateSlotStorage::UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	const UPersistentStateSettings* Settings = UPersistentStateSettings::Get();
	
	TSharedPtr<FUpdateAvailableSlotsAsyncTask, ESPMode::ThreadSafe> Task = MakeShared<FUpdateAvailableSlotsAsyncTask>();
	Task->Path = Settings->GetSaveGamePath();
	Task->Extension = Settings->GetSaveGameExtension();
	Task->DefaultDescriptor = Settings->DefaultSlotDescriptor;
	
	for (const FPersistentStateDefaultNamedSlot& Entry: Settings->DefaultNamedSlots)
	{
		FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(Entry.SlotName, Entry.Title, Entry.Descriptor);
		Task->NamedSlots.Add(Slot);
	}

	FGraphEventArray Prerequisites = GetPrerequisites();
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([Task]
	{
		Task->Run();
	}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	
	LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis=TWeakObjectPtr<ThisClass>{this}, Task, CompletedDelegate]
	{
		if (UPersistentStateSlotStorage* Storage = WeakThis.Get())
		{
			Storage->CompleteSlotUpdate_GameThread(*Task, CompletedDelegate);
		}
	}, TStatId{}, LastQueuedEvent, ENamedThreads::GameThread);

	if (UPersistentStateSettings::Get()->UseGameThread())
	{
		EnsureTaskCompletion();
	}

	return LastQueuedEvent;
}

void UPersistentStateSlotStorage::CompleteSlotUpdate_GameThread(const FUpdateAvailableSlotsAsyncTask& Task, FSlotUpdateCompletedDelegate CompletedDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(IsInGameThread());

	NamedSlots = Task.NamedSlots;
	RuntimeSlots = Task.RuntimeSlots;
	
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

FPersistentStateSlotHandle UPersistentStateSlotStorage::CreateStateSlot(const FName& SlotName, const FText& Title, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(IsInGameThread());

	// ensure that any update tasks are completed, to ensure against the following call sequence:
	// UpdateAvailableSlots - schedule async task to discover save game files and create state slots
	// CreateStateSlot - creates a new state slot.
	// 1. UpdateAvailableSlots finishes and deletes already created slot.
	// 2. If it doesn't, then it can create a SlotName collision, as state slot with the same name can already exist in the file system
	EnsureTaskCompletion();
	
	if (FPersistentStateSlotSharedRef Slot = FindSlot(SlotName); Slot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: trying to create slot with name %s that already exists."), *FString(__FUNCTION__), *SlotName.ToString());
		return FPersistentStateSlotHandle{*this, SlotName};
	}

	if (DescriptorClass == nullptr)
	{
		DescriptorClass = DefaultDescriptor;
	}
	
	FPersistentStateSlotSharedRef Slot = MakeShared<FPersistentStateSlot>(SlotName, Title, DescriptorClass);
	RuntimeSlots.Add(Slot);
	
	const FPersistentStateSlotHandle SlotHandle = FPersistentStateSlotHandle{*this, SlotName};
	const FString FilePath = UPersistentStateSettings::Get()->GetSaveGameFilePath(Slot->GetSlotName());

	CreateStateSlotFile(Slot, FilePath);
	
	return SlotHandle;
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

UPersistentStateSlotDescriptor* UPersistentStateSlotStorage::GetStateSlotDescriptor(const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle);
	if (!StateSlot.IsValid())
	{
		return nullptr;
	}
	
	return StateSlot->CreateSerializedDescriptor(GetWorld(), *StateSlot, SlotHandle);
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

	if (!HasStateSlotFile(StateSlot))
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
	return bNamedSlot || HasStateSlotFile(StateSlot);
}

void UPersistentStateSlotStorage::RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(IsInGameThread());

	// ensure that any update tasks are completed, to ensure against the following call sequence:
	// UpdateAvailableSlots - schedule async task to discover save game files and create state slots.
	// RemoveStateSlot - removes existing state slot.
	// Current implementation will result in UpdateAvailableSlots re-creaing deleted state slot at the end.
	EnsureTaskCompletion();
	
	bool bNamedSlot = false;
	FPersistentStateSlotSharedRef StateSlot = FindSlot(SlotHandle, &bNamedSlot);
	if (!StateSlot.IsValid())
	{
		return;
	}

	if (CurrentSlot.GetSlotName() == StateSlot->GetSlotName())
	{
		// remove cached game data if slot is being removed
		CurrentSlot = {};
		CurrentGameState.Reset();
		CurrentWorldState.Reset();
	}
	
	if (HasStateSlotScreenshotFile(StateSlot))
	{
		// delete screenshot file
		const FString ScreenshotFilePath = UPersistentStateSettings::Get()->GetScreenshotFilePath(StateSlot->GetSlotName());
		RemoveStateSlotFile(ScreenshotFilePath);
	}
	
	if (StateSlot->HasFilePath())
	{
		// launch async task to remove file associated with a slot
		// we can't remove a storage file right away, as they're may be already launched save/load ops
		FGraphEventArray Prerequisites = GetPrerequisites();
		LastQueuedEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([FilePath=StateSlot->GetFilePath()]
		{
			if (!FilePath.IsEmpty())
			{
				RemoveStateSlotFile(FilePath);
			}
		}, TStatId{}, &Prerequisites, ENamedThreads::AnyHiPriThreadNormalTask);
	}

	if (bNamedSlot)
	{
		// reset file data for named slots
		StateSlot->ResetFileState();
	}
	else
	{
		// remove runtime slots entirely
		RuntimeSlots.RemoveSwap(StateSlot);
	}

	// remove from queued slots for screenshot capture
	SlotsForScreenshotCapture.Remove(SlotHandle);
}

void UPersistentStateSlotStorage::AsyncSaveState(
	const FPersistentStateSlotSaveRequest& Request,
	FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot,
	const FString& FilePath,
	TSubclassOf<UPersistentStateSlotDescriptor> DefaultDescriptor
)
{
	check(Request.IsValid());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	FPersistentStateSlotSharedRef Slot = TargetSlot;
	if (FPersistentStateFormatter::IsDebugFormatter())
	{
		// create a temporary proxy slot that saves to a file with a different extension
		Slot = MakeShared<FPersistentStateSlot>(Slot->GetSlotName(), Slot->GetSlotTitle(), DefaultDescriptor);
		
		const FString DebugFilePath = FPaths::ChangeExtension(FilePath, FPersistentStateFormatter::GetExtension());
		CreateStateSlotFile(Slot, DebugFilePath);
		
		Slot->SaveStateDirect(
			Request,
			[](const FString& FilePath) { return CreateStateSlotWriter(FilePath); }
		);
	}
	else
	{
		if (!Slot->HasFilePath())
		{
			CreateStateSlotFile(Slot, FilePath);
		}
		
		Slot->SaveState(
			*SourceSlot, Request,
			[](const FString& FilePath) { return CreateStateSlotReader(FilePath); },
			[](const FString& FilePath) { return CreateStateSlotWriter(FilePath); }
		);
	}
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

bool UPersistentStateSlotStorage::HasStateSlotScreenshotFile(const FPersistentStateSlotSharedRef& Slot)
{
	check(IsInGameThread());
	
	const FString FilePath = UPersistentStateSettings::Get()->GetScreenshotFilePath(Slot->GetSlotName());
	return IFileManager::Get().FileExists(*FilePath);
}

bool UPersistentStateSlotStorage::HasStateSlotFile(const FPersistentStateSlotSharedRef& Slot)
{
	return Slot->HasFilePath() && IFileManager::Get().FileExists(*Slot->GetFilePath());
}

void UPersistentStateSlotStorage::CreateStateSlotFile(const FPersistentStateSlotSharedRef& Slot, const FString& FilePath)
{
	check(Slot.IsValid() && !Slot->HasFilePath());
	// create file and associate it with a slot
	Slot->SetFilePath(FilePath);
	TUniquePtr<FArchive> Archive = CreateStateSlotWriter(FilePath);

	UE_LOG(LogPersistentState, Verbose, TEXT("SaveGame file is created: %s"), *FilePath);
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateStateSlotReader(const FString& FilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("StateSlot file reader: %s"), *FilePath);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileReader(*FilePath, FILEREAD_Silent)};
}

TUniquePtr<FArchive> UPersistentStateSlotStorage::CreateStateSlotWriter(const FString& FilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("StateSlot file writer: %s"), *FilePath);
	
	IFileManager& FileManager = IFileManager::Get();
	return TUniquePtr<FArchive>{FileManager.CreateFileWriter(*FilePath, FILEWRITE_Silent | FILEWRITE_EvenIfReadOnly)};
}

void UPersistentStateSlotStorage::RemoveStateSlotFile(const FString& FilePath)
{
	UE_LOG(LogPersistentState, Verbose, TEXT("StateSlot file removed: %s"), *FilePath);
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

