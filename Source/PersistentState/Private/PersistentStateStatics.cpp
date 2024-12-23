#include "PersistentStateStatics.h"

#include "PersistentStateArchive.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSlot.h"
#include "PersistentStateModule.h"
#include "HAL/ThreadHeartBeat.h"
#include "Managers/PersistentStateManager.h"

namespace UE::PersistentState
{
// @todo: temporary solution to remap original world package to a new world package,
// so that worlds loaded into different packages have compatible save data
// @todo: fixme!!!
extern FString GCurrentWorldPackage;
static FName StaticActorTag{TEXT("PersistentState_Static")};
static FName DynamicActorTag{TEXT("PersistentState_Dynamic")};

void ScheduleAsyncComplete(TFunction<void()> Callback)
{
	// NB. Using Ticker because AsyncTask may run during async package loading which may not be suitable for save data
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Callback = MoveTemp(Callback)](float) -> bool
		{
			Callback();
			return false;
		}
	));
}

void WaitForTask(UE::Tasks::FTask Task)
{
	// need to pump messages on the game thread
	if (IsInGameThread())
	{
		// Suspend the hang and hitch heartbeats, as this is a long running task.
		FSlowHeartBeatScope SuspendHeartBeat;
		FDisableHitchDetectorScope SuspendGameThreadHitch;

		while (!Task.IsCompleted())
		{
			FPlatformMisc::PumpMessagesOutsideMainLoop();
		}
	}
	else
	{
		// not running on the game thread, so just block until the async operation comes back
		const bool bResult = Task.BusyWait();
		check(bResult);
	}
}

void MarkActorStatic(AActor& Actor)
{
	// add unique just to be safe in case Tags are going to be stored as persistent state
	Actor.Tags.AddUnique(StaticActorTag);
}

void MarkActorDynamic(AActor& Actor)
{
	// add unique just to be safe in case Tags are going to be stored as persistent state
	Actor.Tags.AddUnique(DynamicActorTag);
}

void MarkComponentStatic(UActorComponent& Component)
{
	// add unique just to be safe in case Tags are going to be stored as persistent state
	Component.ComponentTags.AddUnique(StaticActorTag);
}

void MarkComponentDynamic(UActorComponent& Component)
{
	// add unique just to be safe in case Tags are going to be stored as persistent state
	Component.ComponentTags.AddUnique(DynamicActorTag);
}

bool IsActorStatic(const AActor& Actor)
{
	return Actor.Tags.Contains(StaticActorTag);
}

bool IsActorDynamic(const AActor& Actor)
{
	return Actor.Tags.Contains(DynamicActorTag);
}

bool IsStaticComponent(const UActorComponent& Component)
{
	return Component.ComponentTags.Contains(StaticActorTag);
}

bool IsDynamicComponent(const UActorComponent& Component)
{
	return Component.ComponentTags.Contains(DynamicActorTag);
}

void ResetSaveGames(const FString& Path, const FString& Extension)
{
	// clean save directory
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (const TCHAR* Directory = *Path; PlatformFile.DirectoryExists(Directory))
	{
		TArray<FString> Files;
		PlatformFile.FindFilesRecursively(Files, Directory, *Extension);

		for (const FString& FileName : Files)
		{
			PlatformFile.DeleteFile(*FileName);
		}

		PlatformFile.DeleteDirectory(Directory);
	}
}

bool HasStableName(const UObject& Object)
{
	return !GetStableName(Object).IsEmpty();
}

FString GetStableName(const UObject& Object)
{
	FString PathName{};
	// full name is stable
	if (Object.IsFullNameStableForNetworking())
	{
		PathName = Object.GetPathName();
	}

	// we have a stable subobject OR a stable name and outer already has a "stable" id which we will use as a name
	// It handles following cases:
	// - default component of a dynamically created actor
	// - blueprint created component of static or dynamic actor
	// - game instance and world subsystems
	else if (Object.IsDefaultSubobject() || Object.IsNameStableForNetworking())
	{
		if (UObject* Outer = Object.GetOuter())
		{
			if (FPersistentStateObjectId OuterId = FPersistentStateObjectId::FindObjectId(Outer); OuterId.IsValid())
			{
				PathName = OuterId.ToString() + TEXT(".") + Object.GetName();
			}
		}
	}

	// object is stable because it is global
	else if (const USubsystem* Subsystem = Cast<USubsystem>(&Object))
	{
		const UObject* Outer = Object.GetOuter();
		PathName = GetStableName(*Outer) + TEXT(".") + Subsystem->GetClass()->GetName();
	}

	// Object overrides its stable name, outer chain still has to be stable. It handles game mode, game state,
	// player controller and other actors that game creates on start
	else if (const IPersistentStateObject* State = Cast<IPersistentStateObject>(&Object))
	{
		if (FName StableName = State->GetStableName(); StableName != NAME_None)
		{
			UObject* Outer = Object.GetOuter();
			check(Outer);
			if (FString OuterStableName = GetStableName(*Outer); !OuterStableName.IsEmpty())
			{
				PathName = OuterStableName + TEXT(".") + StableName.ToString();
			}
			else
			{
				UE_LOG(LogPersistentState, Error, TEXT("%s: object %s provides a stable name override, hovewer its outer chain %s is not stable."),
					*FString(__FUNCTION__), *Object.GetName(), *Outer->GetPathName(Object.GetPackage()));
			}
		}
	}

#if WITH_EDITOR
	if (!GCurrentWorldPackage.IsEmpty())
	{
		// remap main world objects to the original package name, ignore streaming levels
		UWorld* OwningWorld = Object.GetWorld();
		if (OwningWorld != nullptr && Object.GetTypedOuter<UWorld>() == OwningWorld)
		{
			const FString PackageName = OwningWorld->GetPackage()->GetName();
			if (PackageName != GCurrentWorldPackage && PathName.Contains(PackageName))
			{
				const int32 PackageNameEndIndex = PackageName.Len();
				const FString OldPackageName = GCurrentWorldPackage;
				PathName = OldPackageName + PathName.RightChop(PackageNameEndIndex);
			}
		}
	}
#endif

	return PathName;
}
	
void LoadWorldState(TConstArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState)
{
	if (Managers.Num() == 0)
	{
		// nothing to load
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("%s: world %s, chunk count %d"), *FString(__FUNCTION__), *WorldState->Header.WorldName, WorldState->Header.ChunkCount);
	
	FPersistentStateMemoryReader StateReader{WorldState->Data, true};
	FPersistentStateProxyArchive StateArchive{StateReader};

	check(StateArchive.Tell() == 0);
	WorldState->Header.CheckValid();
	GCurrentWorldPackage = WorldState->Header.WorldPackageName;
	check(!GCurrentWorldPackage.IsEmpty());
	
	LoadManagerState(StateArchive, Managers, WorldState->Header.ChunkCount, WorldState->Header.ObjectTablePosition, WorldState->Header.StringTablePosition);
}

void LoadGameState(TConstArrayView<UPersistentStateManager*> Managers, const FGameStateSharedRef& GameState)
{
	if (Managers.Num() == 0)
	{
		// nothing to load
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("%s: chunk count %d"), *FString(__FUNCTION__), GameState->Header.ChunkCount);
	
	FPersistentStateMemoryReader StateReader{GameState->Data, true};
	FPersistentStateProxyArchive StateArchive{StateReader};
	check(StateArchive.Tell() == 0);
	GameState->Header.CheckValid();

	LoadManagerState(StateArchive, Managers, GameState->Header.ChunkCount, GameState->Header.ObjectTablePosition, GameState->Header.StringTablePosition);
}
	
FWorldStateSharedRef CreateWorldState(const UWorld& World, TConstArrayView<UPersistentStateManager*> Managers)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("%s: world %s, chunk count %d"), *FString(__FUNCTION__), *World.GetName(), Managers.Num());
	
	FWorldStateSharedRef WorldState = MakeShared<UE::PersistentState::FWorldState>();
	WorldState->Header.ChunkCount = Managers.Num();
	// will be deduced later
	WorldState->Header.DataSize = 0;
	WorldState->Header.WorldName = World.GetName();
	WorldState->Header.WorldPackageName = GCurrentWorldPackage.IsEmpty() ? World.GetPackage()->GetName() : GCurrentWorldPackage;

	FPersistentStateMemoryWriter StateWriter{WorldState->GetData(), true};
	FPersistentStateProxyArchive StateArchive{StateWriter};
	
	const int32 DataStart = StateArchive.Tell();
	SaveManagerState(StateArchive, Managers, WorldState->Header.ObjectTablePosition, WorldState->Header.StringTablePosition);
	const int32 DataEnd = StateArchive.Tell();
	
	WorldState->Header.DataSize = DataEnd - DataStart;
	WorldState->Header.CheckValid();
	
	return WorldState;
}

FGameStateSharedRef CreateGameState(TConstArrayView<UPersistentStateManager*> Managers)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("%s: chunk count %d"), *FString(__FUNCTION__), Managers.Num());

	FGameStateSharedRef GameState = MakeShared<UE::PersistentState::FGameState>();
	GameState->Header.ChunkCount = Managers.Num();
	// will be deduced later
	GameState->Header.DataSize = 0;
	
	FPersistentStateMemoryWriter StateWriter{GameState->GetData(), true};
	FPersistentStateProxyArchive StateArchive{StateWriter};

	const int32 DataStart = StateArchive.Tell();
	SaveManagerState(StateArchive, Managers, GameState->Header.ObjectTablePosition, GameState->Header.StringTablePosition);
	const int32 DataEnd = StateArchive.Tell();

	GameState->Header.DataSize = DataEnd - DataStart;
	GameState->Header.CheckValid();
	
	return GameState;
}
	
void LoadManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32 ChunkCount, uint32 ObjectTablePosition, uint32 StringTablePosition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	FPersistentStateStringTrackerProxy StringProxy{Ar};
	StringProxy.ReadFromArchive(Ar, StringTablePosition);

	FPersistentStateObjectTracker ObjectTracker{};
	FPersistentStateObjectTrackerProxy ObjectProxy{StringProxy, ObjectTracker};
	ObjectProxy.ReadFromArchive(StringProxy, ObjectTablePosition);
	
	for (uint32 Count = 0; Count < ChunkCount; ++Count)
	{
		FPersistentStateDataChunkHeader ChunkHeader{};
		ObjectProxy << ChunkHeader;
		check(ChunkHeader.IsValid());

		UClass* ChunkClass = ChunkHeader.ChunkType.ResolveClass();
		if (ChunkClass == nullptr)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: failed to find state manager CLASS %s required by a chunk header."), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());
			// skip chunk data
			ObjectProxy.Seek(ObjectProxy.Tell() + ChunkHeader.ChunkSize);
			continue;
		}
		
		UPersistentStateManager* const* ManagerPtr = Managers.FindByPredicate([ChunkClass](const UPersistentStateManager* Manager)
		{
			return ChunkClass == Manager->GetClass();
		});
		if (ManagerPtr == nullptr)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: failed to find state manager INSTANCE %s required by a chunk header."), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());
			// skip chunk data
			ObjectProxy.Seek(ObjectProxy.Tell() + ChunkHeader.ChunkSize);
			continue;
		}

		UE_LOG(LogPersistentState, Verbose, TEXT("%s: serialized state manager %s"), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());

		{
			UPersistentStateManager* StateManager = *ManagerPtr;
			FScopeCycleCounterUObject Scope{StateManager};
			StateManager->Serialize(ObjectProxy);
		}
	}
}
	
void SaveManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32& OutObjectTablePosition, uint32& OutStringTablePosition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	FPersistentStateStringTrackerProxy StringProxy{Ar};
	{
		FPersistentStateObjectTracker ObjectTracker{};
		FPersistentStateObjectTrackerProxy ObjectProxy{StringProxy, ObjectTracker};
		for (UPersistentStateManager* StateManager : Managers)
		{
			FScopeCycleCounterUObject Scope{StateManager};
			FPersistentStateDataChunkHeader ChunkHeader{StateManager->GetClass(), 0};
			UE_LOG(LogPersistentState, Verbose, TEXT("%s: serialized state manager %s"), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());
			
			const int32 ChunkHeaderPosition = ObjectProxy.Tell();
			ObjectProxy << ChunkHeader;

			const int32 ChunkStartPosition = ObjectProxy.Tell();
			StateManager->Serialize(ObjectProxy);
			const int32 ChunkEndPosition = ObjectProxy.Tell();
		
			ObjectProxy.Seek(ChunkHeaderPosition);

			// override chunk header data with new chunk size data
			ChunkHeader.ChunkSize = ChunkEndPosition - ChunkStartPosition;
			// set archive to a chunk header position
			ObjectProxy.Seek(ChunkHeaderPosition);
			ObjectProxy << ChunkHeader;
			// set archive to point at the end position
			ObjectProxy.Seek(ChunkEndPosition);
		}

		OutObjectTablePosition = StringProxy.Tell();
		ObjectProxy.WriteToArchive(StringProxy);
	}

	OutStringTablePosition = StringProxy.Tell();
	StringProxy.WriteToArchive(StringProxy);
}


void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryReader Reader{SaveGameBunch, true};
	Reader.SetWantBinaryPropertySerialization(true);
	Reader.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Reader};

	Object.Serialize(Archive);
}

void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryWriter Writer{SaveGameBunch, true};
	Writer.SetWantBinaryPropertySerialization(true);
	Writer.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Writer};

	Object.Serialize(Archive);
}

void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryReader Reader{SaveGameBunch, true};
	Reader.SetWantBinaryPropertySerialization(true);
	Reader.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Reader};
	FPersistentStateObjectTrackerProxy ObjectProxy{Archive, ObjectTracker, EObjectDependency::Hard};

	Object.Serialize(ObjectProxy);
}

void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryWriter Writer{SaveGameBunch, true};
	Writer.SetWantBinaryPropertySerialization(true);
	Writer.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Writer};
	FPersistentStateObjectTrackerProxy ObjectProxy{Archive, ObjectTracker, EObjectDependency::Hard};

	Object.Serialize(ObjectProxy);
}
	
}
