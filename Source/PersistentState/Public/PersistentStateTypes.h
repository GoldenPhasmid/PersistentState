#pragma once

#include "CoreMinimal.h"
#include "PersistentStateDefines.h"
#include "Serialization/ArchiveProxy.h"

#include "PersistentStateTypes.generated.h"

class AActor;
class UActorComponent;
class USceneComponent;
class IPersistentStateObject;

static constexpr int32 SLOT_HEADER_TAG	= 0x53A41B6D;
static constexpr int32 WORLD_HEADER_TAG = 0x3AEF241C;

namespace UE::PersistentState
{
	struct PERSISTENTSTATE_API FWorldState
	{
		FWorldState(FName InWorld)
			: World(InWorld)
		{}
		
		FName World = NAME_None;
		TArray<uint8> Data;

		FName GetWorld() const { return World; }
		TArray<uint8>& GetData() { return Data; }
		const TArray<uint8>& GetData() const { return Data; }
	};
}

using FWorldStateSharedRef = TSharedPtr<UE::PersistentState::FWorldState, ESPMode::ThreadSafe>;

template <typename TStructType>
FArchive& operator<<(FArchive& Ar, TStructType& Value)
{
	TStructType::StaticStruct()->SerializeItem(Ar, &Value, nullptr);
	return Ar;
}

/**
 * 
 */
USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateDataChunkHeader
{
	GENERATED_BODY()
public:
	/** chunk type */
	FString ChunkType;
	/** chunk length, excluding header size */
	uint32 ChunkSize = 0;

	FPersistentStateDataChunkHeader() = default;
	FPersistentStateDataChunkHeader(const UClass* InChunkType, uint32 InChunkSize)
		: ChunkType(InChunkType->GetPathName())
		, ChunkSize(InChunkSize)
	{}

	bool IsValid() const
	{
		return ChunkSize > 0 && !ChunkType.IsEmpty();
	}
	
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateDataChunkHeader& Value)
	{
		Ar << Value.ChunkType;
		Ar << Value.ChunkSize;

		return Ar;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
};

template <>
struct TStructOpsTypeTraits<FPersistentStateDataChunkHeader>: public TStructOpsTypeTraitsBase2<FPersistentStateDataChunkHeader>
{
	enum
	{
		WithSerializer = true
	};
};

USTRUCT()
struct PERSISTENTSTATE_API FWorldStateDataHeader
{
	GENERATED_BODY()

	UPROPERTY()
	int32 WorldHeaderTag = WORLD_HEADER_TAG;
	
	/** world name that uniquely identifies it in the save file */
	UPROPERTY()
	FName WorldName = NAME_None;

	/** world data start position in the save file */
	uint32 WorldDataPosition = TNumericLimits<uint32>::Max();

	/** world data length in bytes in the save file */
	UPROPERTY()
	uint32 WorldDataSize = TNumericLimits<uint32>::Max();

	/** number of world managers stored as a part of world data */
	UPROPERTY()
	uint32 ManagerCount = TNumericLimits<uint32>::Max();
};

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateSlotHeader
{
	GENERATED_BODY()

	/** */
	UPROPERTY()
	int32 SlotHeaderTag = SLOT_HEADER_TAG;
	
	/** Logical save slot name */
	UPROPERTY()
	FString SlotName;
	
	/** Display title text */
	UPROPERTY()
	FText Title;

	/** Timestamp when storage was created */
	UPROPERTY()
	FDateTime Timestamp;

	/** last saved world */
	UPROPERTY()
	FName LastSavedWorld;
	
	/** number of worlds stored in the slot */
	UPROPERTY()
	int32 WorldCount = 0;

	/** offset from the beginning of the save file to the world data, excluding slot header */
	UPROPERTY()
	uint32 WorldDataOffset = 0;
};

struct FPersistentStateSlot
{
	FPersistentStateSlot() = default;
	/** create state slot from a loaded archive */
	FPersistentStateSlot(FArchive& Ar, const FString& InFilePath, const FString& InFileName);
	/** create a state slot that is not yet associated with any actual data */
	FPersistentStateSlot(const FString& SlotName, const FText& Title)
	{
		Header.SlotName = SlotName;
		Header.Title = Title;
		bValidBit = true;
	}

	/** load world state from a slot archive to a shared data ref */
	FWorldStateSharedRef LoadWorldState(FArchive& Ar, FName WorldName);
	
	FORCEINLINE FArchive& operator<<(FArchive& Ar)
	{
		Ar << Header;
		return Ar;
	}
	
	FORCEINLINE FName GetSlotName() const
	{
		return FName{Header.SlotName};
	}
	
	FORCEINLINE FName GetWorldToLoad() const
	{
		return Header.LastSavedWorld;
	}
	
	FORCEINLINE void UpdateTimestamp()
	{
		Header.Timestamp = FDateTime::Now();
	}

	FORCEINLINE FWorldStateSharedRef GetWorldState() const
	{
		return PreLoadWorldState;
	}

	FORCEINLINE void SetWorldState(FWorldStateSharedRef NewWorldState)
	{
		check(NewWorldState.IsValid());
		PreLoadWorldState = NewWorldState;
	}

	FORCEINLINE void SetLastSavedWorld(FName InWorldName)
	{
		Header.LastSavedWorld = InWorldName;
	}
	
	/** slot header, stored as a part of physical file */
	FPersistentStateSlotHeader Header;

	/** physical file path, if any */
	FString FilePath;

	/** physical file name, if any */
	FString FileName;

	/** list of world headers */
	TArray<FWorldStateDataHeader> WorldHeaders;

private:
	/** pre-loaded world state */
	FWorldStateSharedRef PreLoadWorldState;

	/** valid bit that indicates whether state slot was loaded correctly. Always valid for slots without physical state */
	uint8 bValidBit: 1 = false;
};

