#include "PersistentStateStatics.h"

#include "PersistentStateArchive.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSlot.h"
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

	return FString{};
}
	
void LoadWorldState(TArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState)
{
	FPersistentStateMemoryReader StateReader{WorldState->Data, true};
	FPersistentStateProxyArchive StateArchive{StateReader};
	// StateReader.SetWantBinaryPropertySerialization(true);
	
	FWorldStateDataHeader Header{};
	StateArchive << Header;

	for (uint32 Count = 0; Count < Header.ManagerCount; ++Count)
	{
		FPersistentStateDataChunkHeader ChunkHeader{};
		StateArchive << ChunkHeader;
		check(ChunkHeader.IsValid());

		UClass* ChunkClass = UClass::TryFindTypeSlow<UClass>(ChunkHeader.ChunkType, EFindFirstObjectOptions::ExactClass | EFindFirstObjectOptions::EnsureIfAmbiguous);
		check(ChunkClass);
		
		UPersistentStateManager** ManagerPtr = Managers.FindByPredicate([ChunkClass](const UPersistentStateManager* Manager)
		{
			return ChunkClass == Manager->GetClass();
		});
		if (ManagerPtr == nullptr)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: failed to find world state manager %s from a chunk header"), *FString(__FUNCTION__), *ChunkHeader.ChunkType);
			// skip chunk data
			StateArchive.Seek(StateArchive.Tell() + ChunkHeader.ChunkSize);
			continue;
		}

		UPersistentStateManager* StateManager = *ManagerPtr;
		StateManager->Serialize(StateArchive);
	}
}

FWorldStateSharedRef SaveWorldState(UWorld* World, TArrayView<UPersistentStateManager*> Managers)
{
	FWorldStateSharedRef WorldState = MakeShared<UE::PersistentState::FWorldState>(World->GetFName());
	
	FPersistentStateMemoryWriter StateWriter{WorldState->GetData(), true};
	FPersistentStateProxyArchive StateArchive{StateWriter};

	FWorldStateDataHeader WorldHeader{};
	WorldHeader.WorldName = World->GetFName();
	WorldHeader.ManagerCount = Managers.Num();
	// will be deduced later
	WorldHeader.WorldDataSize = 0;

	const int32 HeaderPosition = StateArchive.Tell();
	StateArchive << WorldHeader;
	const int32 StartPosition = StateArchive.Tell();
	
	for (UPersistentStateManager* StateManager : Managers)
	{
		FPersistentStateDataChunkHeader ChunkHeader{StateManager->GetClass(), 0};

		const int32 ChunkHeaderPosition = StateArchive.Tell();
		StateArchive << ChunkHeader;

		const int32 ChunkStartPosition = StateArchive.Tell();
		StateManager->Serialize(StateArchive);
		const int32 ChunkEndPosition = StateArchive.Tell();
		
		StateArchive.Seek(ChunkHeaderPosition);

		// override chunk header data with new chunk size data
		ChunkHeader.ChunkSize = ChunkEndPosition - ChunkStartPosition;
		// set archive to a chunk header position
		StateArchive.Seek(ChunkHeaderPosition);
		StateArchive << ChunkHeader;
		StateArchive.Seek(ChunkEndPosition);
	}

	const int32 EndPosition = StateArchive.Tell();
	WorldHeader.WorldDataSize = EndPosition - StartPosition;
	WorldHeader.WorldDataPosition = StartPosition;
	
	StateArchive.Seek(HeaderPosition);
	StateArchive << WorldHeader;
	StateArchive.Seek(EndPosition);
	
	return WorldState;
}

void LoadObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch)
{
	FPersistentStateMemoryWriter Writer{SaveGameBunch, true};
	Writer.SetWantBinaryPropertySerialization(true);
	Writer.ArIsSaveGame = true;
	
	FPersistentStateProxyArchive Archive{Writer};

	Object.Serialize(Archive);
}

void SaveObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch)
{
	FPersistentStateMemoryReader Reader{SaveGameBunch, true};
	Reader.SetWantBinaryPropertySerialization(true);
	Reader.ArIsSaveGame = true;
	
	FPersistentStateProxyArchive Archive{Reader};

	Object.Serialize(Archive);
}
	
}
