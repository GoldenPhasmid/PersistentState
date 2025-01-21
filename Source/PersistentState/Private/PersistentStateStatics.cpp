#include "PersistentStateStatics.h"

#include "ImageUtils.h"
#include "PersistentStateArchive.h"
#include "PersistentStateCVars.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSlot.h"
#include "PersistentStateModule.h"
#include "PersistentStateSerialization.h"

#include "Managers/PersistentStateManager.h"
#include "HAL/ThreadHeartBeat.h"

namespace UE::PersistentState
{
static FName StaticActorTag{TEXT("PersistentState_Static")};
static FName DynamicActorTag{TEXT("PersistentState_Dynamic")};

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

#if WITH_EDITOR_COMPATIBILITY
	PathName = FPersistentStateObjectPathGenerator::Get().RemapObjectPath(Object, PathName);
#endif

	return PathName;
}
	
bool HasStableName(const UObject& Object)
{
	return !GetStableName(Object).IsEmpty();
}

void SanitizeReference(const UObject& SourceObject, const UObject* ReferenceObject)
{
#if WITH_SANITIZE_REFERENCES
	if (!UE::PersistentState::GPersistentState_SanitizeObjectReferences)
	{
		return;
	}
	
	if (ReferenceObject == nullptr)
	{
		return;
	}

	const ULevel* SourceLevel = SourceObject.GetTypedOuter<ULevel>();
	const ULevel* ReferenceLevel = ReferenceObject->GetTypedOuter<ULevel>();
	
	const FPersistentStateObjectId SourceId = FPersistentStateObjectId::FindObjectId(&SourceObject);
	const FPersistentStateObjectId ReferenceId = FPersistentStateObjectId::FindObjectId(ReferenceObject);

	if (SourceId.IsValid() && !ReferenceId.IsValid() && !ReferenceObject->IsA<UPackage>())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Object [%s] references [%s] without a valid ID."),
			*FString(__FUNCTION__), *SourceId.GetObjectName(), *ReferenceObject->GetName());
	}
	
	// global object not owned by the level (e.g. subsystem) references object owned by the level
	if (SourceLevel == nullptr && ReferenceLevel != nullptr)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Object [%s] not level owned references level owned object [%s]."),
			*FString(__FUNCTION__), *SourceId.GetObjectName(), *ReferenceId.GetObjectName());
	}

	// objects owned by different levels, and reference level is not persistent
	if (SourceLevel != nullptr && ReferenceLevel != nullptr)
	{
		if (SourceLevel != ReferenceLevel && !ReferenceLevel->IsPersistentLevel())
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: Object [%s] references object [%s] from another (non-persistent) level."),
				*FString(__FUNCTION__), *SourceId.GetObjectName(), *ReferenceId.GetObjectName());
		}
	}
#endif // WITH_SANITIZE_REFERENCES
}

bool LoadScreenshot(const FString& FilePath, FImage& Image)
{
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return false;
	}
	
	TArray64<uint8> CompressedData;
	FFileHelper::LoadFileToArray(CompressedData, *FilePath);

	return FImageUtils::DecompressImage(CompressedData.GetData(), CompressedData.Num(), Image);
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
	StateReader.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	FPersistentStateProxyArchive StateArchive{StateReader};

	check(StateArchive.Tell() == 0);
	WorldState->Header.CheckValid();
	
	Private::LoadManagerState(StateArchive, Managers, WorldState->Header.ChunkCount, WorldState->Header.ObjectTablePosition, WorldState->Header.StringTablePosition);
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
	StateReader.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	FPersistentStateProxyArchive StateArchive{StateReader};
	check(StateArchive.Tell() == 0);
	GameState->Header.CheckValid();

	Private::LoadManagerState(StateArchive, Managers, GameState->Header.ChunkCount, GameState->Header.ObjectTablePosition, GameState->Header.StringTablePosition);
}
	
FWorldStateSharedRef CreateWorldState(const FString& World, const FString& WorldPackage, TConstArrayView<UPersistentStateManager*> Managers)
{
	check(!World.IsEmpty() && !WorldPackage.IsEmpty());
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	UE_LOG(LogPersistentState, Verbose, TEXT("%s: world %s, chunk count %d"), *FString(__FUNCTION__), *World, Managers.Num());
	
	FWorldStateSharedRef WorldState = MakeShared<UE::PersistentState::FWorldState>();
	WorldState->Header.ChunkCount = Managers.Num();
	// will be deduced later
	WorldState->Header.DataSize = 0;
	WorldState->Header.WorldName = World;
	WorldState->Header.WorldPackageName = WorldPackage;

	if (Managers.Num() > 0)
	{
		FPersistentStateMemoryWriter StateWriter{WorldState->GetData(), true};
		StateWriter.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
		FPersistentStateProxyArchive StateArchive{StateWriter};
	
		const int32 DataStart = StateArchive.Tell();
		Private::SaveManagerState(StateArchive, Managers, WorldState->Header.ObjectTablePosition, WorldState->Header.StringTablePosition);
		const int32 DataEnd = StateArchive.Tell();
		
		WorldState->Header.DataSize = DataEnd - DataStart;
	}
	
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
	StateWriter.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	FPersistentStateProxyArchive StateArchive{StateWriter};

	if (Managers.Num() > 0)
	{
		const int32 DataStart = StateArchive.Tell();
		Private::SaveManagerState(StateArchive, Managers, GameState->Header.ObjectTablePosition, GameState->Header.StringTablePosition);
		const int32 DataEnd = StateArchive.Tell();

		GameState->Header.DataSize = DataEnd - DataStart;
		GameState->Header.CheckValid();
	}
	
	return GameState;
}

namespace Private
{
	
void LoadManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32 ChunkCount, uint32 ObjectTablePosition, uint32 StringTablePosition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	constexpr bool bLoading = true;

	FPersistentStateStringTrackerProxy<bLoading> StringProxy{Ar};
	StringProxy.ReadFromArchive(Ar, StringTablePosition);

	FPersistentStateObjectTracker ObjectTracker{};
	FPersistentStateObjectTrackerProxy<bLoading, ESerializeObjectDependency::All> ObjectProxy{StringProxy, ObjectTracker};
	ObjectProxy.ReadFromArchive(StringProxy, ObjectTablePosition);

	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateLoadFormatter(ObjectProxy);
	FStructuredArchive StructuredArchive{*Formatter};
	FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();

	for (uint32 Count = 0; Count < ChunkCount; ++Count)
	{
		FPersistentStateDataChunkHeader ChunkHeader{};
		RootRecord << SA_VALUE(TEXT("ChunkHeader"), ChunkHeader);
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

		UPersistentStateManager* StateManager = *ManagerPtr;
		StateManager->PreLoadState();
		{
			FScopeCycleCounterUObject Scope{StateManager};
			StateManager->Serialize(RootRecord);
		}
		StateManager->PostLoadState();
	}
}

void SaveManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32& OutObjectTablePosition, uint32& OutStringTablePosition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	constexpr bool bLoading = false;
	FPersistentStateStringTrackerProxy<bLoading> StringProxy{Ar};
	{
		FPersistentStateObjectTracker ObjectTracker{};
		FPersistentStateObjectTrackerProxy<bLoading, ESerializeObjectDependency::All> ObjectProxy{StringProxy, ObjectTracker};

		TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateSaveFormatter(ObjectProxy);
		FStructuredArchive StructuredArchive{*Formatter};
		FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();
	
		for (UPersistentStateManager* StateManager : Managers)
		{
			FScopeCycleCounterUObject Scope{StateManager};
			FPersistentStateDataChunkHeader ChunkHeader{StateManager->GetClass(), 0};
			UE_LOG(LogPersistentState, Verbose, TEXT("%s: serialized state manager %s"), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());
		
			const int32 ChunkHeaderPosition = ObjectProxy.Tell();
			RootRecord << SA_VALUE(TEXT("ChunkHeader"), ChunkHeader);

			const int32 ChunkStartPosition = ObjectProxy.Tell();
			StateManager->Serialize(RootRecord);
			const int32 ChunkEndPosition = ObjectProxy.Tell();
	
			ObjectProxy.Seek(ChunkHeaderPosition);

			// override chunk header data with new chunk size data
			ChunkHeader.ChunkSize = ChunkEndPosition - ChunkStartPosition;
			// set archive to a chunk header position
			ObjectProxy.Seek(ChunkHeaderPosition);
			RootRecord << SA_VALUE(TEXT("ChunkHeader"), ChunkHeader);
			// set archive to point at the end position
			ObjectProxy.Seek(ChunkEndPosition);
		}

		OutObjectTablePosition = StringProxy.Tell();
		ObjectProxy.WriteToArchive(StringProxy);
	}

	OutStringTablePosition = StringProxy.Tell();
	StringProxy.WriteToArchive(Ar);
}
} // Private

void LoadObject(UObject& Object, const FPersistentStatePropertyBunch& PropertyBunch, bool bIsSaveGame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryReader Reader{PropertyBunch.Value, true};
	Reader.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	Reader.ArIsSaveGame = bIsSaveGame;
	
	FPersistentStateSaveGameArchive Archive{Reader, Object};
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateLoadFormatter(Archive);
	FStructuredArchive StructuredArchive{*Formatter};

	Object.Serialize(StructuredArchive.Open().EnterRecord());
}

void SaveObject(UObject& Object, FPersistentStatePropertyBunch& PropertyBunch, bool bIsSaveGame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryWriter Writer{PropertyBunch.Value, true};
	Writer.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	Writer.ArIsSaveGame = bIsSaveGame;
	
	FPersistentStateSaveGameArchive Archive{Writer, Object};
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateSaveFormatter(Archive);
	FStructuredArchive StructuredArchive{*Formatter};

	Object.Serialize(StructuredArchive.Open().EnterRecord());
}

void LoadObject(UObject& Object, const FPersistentStatePropertyBunch& PropertyBunch, FPersistentStateObjectTracker& DependencyTracker, bool bIsSaveGame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryReader Reader{PropertyBunch.Value, true};
	Reader.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	Reader.ArIsSaveGame = bIsSaveGame;
	
	FPersistentStateSaveGameArchive Archive{Reader, Object};
	
	constexpr bool bLoading = true;
	FPersistentStateObjectTrackerProxy<bLoading, ESerializeObjectDependency::Hard> ObjectProxy{Archive, DependencyTracker};
	
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateLoadFormatter(Archive);
	FStructuredArchive StructuredArchive{*Formatter};
	Object.Serialize(StructuredArchive.Open().EnterRecord());
}

void SaveObject(UObject& Object, FPersistentStatePropertyBunch& SaveGameBunch, FPersistentStateObjectTracker& DependencyTracker, bool bIsSaveGame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	FScopeCycleCounterUObject Scope{&Object};
	
	FPersistentStateMemoryWriter Writer{SaveGameBunch.Value, true};
	Writer.SetWantBinaryPropertySerialization(WITH_BINARY_SERIALIZATION);
	Writer.ArIsSaveGame = bIsSaveGame;
	
	FPersistentStateSaveGameArchive Archive{Writer, Object};
	
	constexpr bool bLoading = false;
	FPersistentStateObjectTrackerProxy<bLoading, ESerializeObjectDependency::Hard> ObjectProxy{Archive, DependencyTracker};

	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateSaveFormatter(Archive);
	FStructuredArchive StructuredArchive{*Formatter};
	Object.Serialize(StructuredArchive.Open().EnterRecord());
}
	
}
