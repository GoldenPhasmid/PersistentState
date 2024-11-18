#include "PersistentStateStatics.h"

#include "PersistentStateArchive.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSlot.h"
#include "PersistentStateDefines.h"
#include "Managers/PersistentStateManager.h"

namespace UE::PersistentState
{
// @todo: temporary solution to remap original world package to a new world package,
// so that worlds loaded into different packages have compatible save data
// @todo: fixme!!!
extern FString GCurrentWorldPackage;
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
		if (Object.GetTypedOuter<UWorld>() == OwningWorld)
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
	
void LoadWorldState(TArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState)
{
	FPersistentStateMemoryReader StateReader{WorldState->Data, true};
	FPersistentStateProxyArchive StateArchive{StateReader};

	check(StateArchive.Tell() == 0);
	WorldState->Header.CheckValid();
	GCurrentWorldPackage = WorldState->Header.WorldPackageName;
	check(!GCurrentWorldPackage.IsEmpty());

	FPersistentStateStringTrackerProxy StringTracker{StateArchive};
	StringTracker.ReadFromArchive(StateArchive, WorldState->Header.StringTablePosition);
	
	FPersistentStateObjectTrackerProxy ObjectTracker{StringTracker};
	ObjectTracker.ReadFromArchive(StringTracker, WorldState->Header.ObjectTablePosition);
	
	for (uint32 Count = 0; Count < WorldState->Header.ChunkCount; ++Count)
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
	
	WorldState->Header.WorldName = World->GetName();
	WorldState->Header.WorldPackageName = GCurrentWorldPackage.IsEmpty() ? World->GetPackage()->GetName() : GCurrentWorldPackage;
	WorldState->Header.ChunkCount = Managers.Num();
	// will be deduced later
	WorldState->Header.WorldDataSize = 0;
	
	const int32 WorldHeaderStart = StateArchive.Tell();
	check(WorldHeaderStart == 0);
	const int32 WorldDataStart = StateArchive.Tell();
	
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

			WorldState->Header.ObjectTablePosition = StringTracker.Tell();
			ObjectTracker.WriteToArchive(StringTracker);
		}

		WorldState->Header.StringTablePosition = StringTracker.Tell();
		StringTracker.WriteToArchive(StringTracker);
	}

	const int32 WorldDataEnd = StateArchive.Tell();
	WorldState->Header.WorldDataSize = WorldDataEnd - WorldDataStart;
	WorldState->Header.CheckValid();
	
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
