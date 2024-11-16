#include "PersistentStateStatics.h"

#include "PersistentStateArchive.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSlot.h"
#include "PersistentStateDefines.h"
#include "Managers/PersistentStateManager.h"

namespace UE::PersistentState
{

static FName StaticActorTag{TEXT("PersistentState_Static")};
static FName DynamicActorTag{TEXT("PersistentState_Dynamic")};
	
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

bool HasStableName(const UObject& Object)
{
	return !GetStableName(Object).IsEmpty();
}

FString GetStableName(const UObject& Object)
{
	// full name is stable
	if (Object.IsFullNameStableForNetworking())
	{
		if (UWorld* World = Object.GetTypedOuter<UWorld>())
		{
			return Object.GetPathName(World->GetPackage());
		}
		else
		{
			return Object.GetPathName(Object.GetPackage());
		}
	}

	// we have a stable subobject OR a stable name and outer already has a "stable" id which we will use as a name
	// It handles following cases:
	// - default component of a dynamically created actor
	// - blueprint created component of static or dynamic actor
	// - game instance and world subsystems
	if (Object.IsDefaultSubobject() || Object.IsNameStableForNetworking())
	{
		if (UObject* Outer = Object.GetOuter())
		{
			if (FPersistentStateObjectId OuterId = FPersistentStateObjectId::FindObjectId(Outer); OuterId.IsValid())
			{
				return OuterId.ToString() + TEXT(".") + Object.GetName();		
			}
		}
	}

	// Object overrides its stable name, outer chain still has to be stable. It handles game mode, game state,
	// player controller and other actors that game creates on start
	if (const IPersistentStateObject* State = Cast<IPersistentStateObject>(&Object))
	{
		if (FName StableName = State->GetStableName(); StableName != NAME_None)
		{
			UObject* Outer = Object.GetOuter();
			check(Outer);
			if (FString OuterStableName = GetStableName(*Outer); !OuterStableName.IsEmpty())
			{
				return OuterStableName + TEXT(".") + StableName.ToString();
			}
			else
			{
				UE_LOG(LogPersistentState, Error, TEXT("%s: object %s provides a stable name override, hovewer its outer chain %s is not stable."),
					*FString(__FUNCTION__), *Object.GetName(), *Outer->GetPathName(Object.GetPackage()));
				return FString{};
			}
		}
	}

	// object is stable because it is global
	if (const USubsystem* Subsystem = Cast<USubsystem>(&Object))
	{
		UObject* Outer = Object.GetOuter();
		return GetStableName(*Outer) + TEXT(".") + Subsystem->GetClass()->GetName();
	}

	return FString{};
}
	
void LoadWorldState(TArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState)
{
	FPersistentStateMemoryReader StateReader{WorldState->Data, true};
	FPersistentStateProxyArchive StateArchive{StateReader};
	
	FWorldStateDataHeader WorldHeader{};
	StateArchive << WorldHeader;
	
	WorldHeader.CheckValid();

	FPersistentStateStringTrackerProxy StringTracker{StateArchive};
	StringTracker.ReadFromArchive(StateArchive, WorldHeader.StringTablePosition);
	
	FPersistentStateObjectTrackerProxy ObjectTracker{StringTracker};
	ObjectTracker.ReadFromArchive(StringTracker, WorldHeader.ObjectTablePosition);
	
	for (uint32 Count = 0; Count < WorldHeader.ChunkCount; ++Count)
	{
		FPersistentStateDataChunkHeader ChunkHeader{};
		ObjectTracker << ChunkHeader;
		check(ChunkHeader.IsValid());

		UClass* ChunkClass = ChunkHeader.ChunkType.ResolveClass();
		check(ChunkClass);
		
		UPersistentStateManager** ManagerPtr = Managers.FindByPredicate([ChunkClass](const UPersistentStateManager* Manager)
		{
			return ChunkClass == Manager->GetClass();
		});
		if (ManagerPtr == nullptr)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: failed to find world state manager %s from a chunk header"), *FString(__FUNCTION__), *ChunkHeader.ChunkType.ToString());
			// skip chunk data
			ObjectTracker.Seek(ObjectTracker.Tell() + ChunkHeader.ChunkSize);
			continue;
		}

		UPersistentStateManager* StateManager = *ManagerPtr;
		StateManager->Serialize(ObjectTracker);
	}
}

FWorldStateSharedRef SaveWorldState(UWorld* World, TArrayView<UPersistentStateManager*> Managers)
{
	FWorldStateSharedRef WorldState = MakeShared<UE::PersistentState::FWorldState>(World->GetFName());
	
	FPersistentStateMemoryWriter StateWriter{WorldState->GetData(), true};
	FPersistentStateProxyArchive StateArchive{StateWriter};

	FWorldStateDataHeader WorldHeader{};
	WorldHeader.WorldName = World->GetName();
	WorldHeader.ChunkCount = Managers.Num();
	// will be deduced later
	WorldHeader.WorldDataSize = 0;
	
	const int32 HeaderPosition = StateArchive.Tell();
	StateArchive << WorldHeader;
	WorldHeader.WorldDataPosition = StateArchive.Tell();

	{
		FPersistentStateStringTrackerProxy StringTracker{StateArchive};
		{
			FPersistentStateObjectTrackerProxy ObjectTracker{StringTracker};
			for (UPersistentStateManager* StateManager : Managers)
			{
				FPersistentStateDataChunkHeader ChunkHeader{StateManager->GetClass(), 0};

				const int32 ChunkHeaderPosition = ObjectTracker.Tell();
				ObjectTracker << ChunkHeader;

				const int32 ChunkStartPosition = ObjectTracker.Tell();
				StateManager->Serialize(ObjectTracker);
				const int32 ChunkEndPosition = ObjectTracker.Tell();
		
				ObjectTracker.Seek(ChunkHeaderPosition);

				// override chunk header data with new chunk size data
				ChunkHeader.ChunkSize = ChunkEndPosition - ChunkStartPosition;
				// set archive to a chunk header position
				ObjectTracker.Seek(ChunkHeaderPosition);
				ObjectTracker << ChunkHeader;
				// set archive to point at the end position
				ObjectTracker.Seek(ChunkEndPosition);
			}

			WorldHeader.ObjectTablePosition = StringTracker.Tell();
			ObjectTracker.WriteToArchive(StringTracker);
		}

		WorldHeader.StringTablePosition = StringTracker.Tell();
		StringTracker.WriteToArchive(StringTracker);
	}

	const int32 EndPosition = StateArchive.Tell();
	WorldHeader.WorldDataSize = EndPosition - WorldHeader.WorldDataPosition;
	WorldHeader.CheckValid();
	
	StateArchive.Seek(HeaderPosition);
	StateArchive << WorldHeader;
	StateArchive.Seek(EndPosition);
	
	return WorldState;
}

void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch)
{
	FPersistentStateMemoryReader Reader{SaveGameBunch, true};
	Reader.SetWantBinaryPropertySerialization(true);
	Reader.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Reader};

	Object.Serialize(Archive);
}

void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch)
{
	FPersistentStateMemoryWriter Writer{SaveGameBunch, true};
	Writer.SetWantBinaryPropertySerialization(true);
	Writer.ArIsSaveGame = true;
	
	FPersistentStateSaveGameArchive Archive{Writer};

	Object.Serialize(Archive);
}
	
}
