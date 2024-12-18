#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

#include "PersistentStateSlot.generated.h"

class AActor;
class UActorComponent;
class USceneComponent;
class IPersistentStateObject;

static constexpr int32 SLOT_HEADER_TAG	= 0x53A41B6D;
static constexpr int32 WORLD_HEADER_TAG = 0x3AEF241C;

/**
 * 
 */
USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateDataChunkHeader
{
	GENERATED_BODY()
public:
	/** chunk type */
	FSoftClassPath ChunkType;
	/** chunk length, excluding header size */
	uint32 ChunkSize = 0;

	FPersistentStateDataChunkHeader() = default;
	FPersistentStateDataChunkHeader(const UClass* InChunkType, uint32 InChunkSize)
		: ChunkType(FSoftClassPath{InChunkType})
		, ChunkSize(InChunkSize)
	{}

	bool IsValid() const
	{
		return ChunkSize > 0 && !ChunkType.IsNull();
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
	
	static constexpr uint32 InvalidSize = TNumericLimits<uint32>::Max();

	void InitializeToEmpty()
	{
		WorldDataStart = WorldDataSize = 0;
		ObjectTablePosition = StringTablePosition = ChunkCount = 0;
	}
	
	void CheckValid() const
	{
		check(!WorldName.IsEmpty());
		check(!WorldPackageName.IsEmpty());
		check(ObjectTablePosition != InvalidSize);
		check(StringTablePosition != InvalidSize);
		check(WorldDataSize != InvalidSize);
		check(ChunkCount != InvalidSize);
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend void operator<<(FArchive& Ar, FWorldStateDataHeader& Value)
	{
		Ar << Value.WorldHeaderTag;
		Ar << Value.WorldName;
		Ar << Value.WorldPackageName;
		Ar << Value.ObjectTablePosition;
		Ar << Value.StringTablePosition;
		Ar << Value.WorldDataStart;
		Ar << Value.WorldDataSize;
		Ar << Value.ChunkCount;
	}

	/** world header magic tag */
	UPROPERTY()
	int32 WorldHeaderTag = WORLD_HEADER_TAG;
	
	/** world name that uniquely identifies it in the save file */
	UPROPERTY()
	FString WorldName;

	/** world package name */
	UPROPERTY()
	FString WorldPackageName;
	
	/** object table position inside world data, can be zero */
	UPROPERTY()
	uint32 ObjectTablePosition = InvalidSize;

	/** string table position inside world data, can be zero */
	UPROPERTY()
	uint32 StringTablePosition = InvalidSize;

	/** world data start position inside the slot save archive, never zero */
	uint32 WorldDataStart = InvalidSize;

	/** world data length in bytes in the save file, including object table and string table, can be zero */
	UPROPERTY()
	uint32 WorldDataSize = InvalidSize;

	/** number of world managers stored as a part of world data */
	UPROPERTY()
	uint32 ChunkCount = InvalidSize;
};

template <>
struct TStructOpsTypeTraits<FWorldStateDataHeader>: public TStructOpsTypeTraitsBase2<FWorldStateDataHeader>
{
	enum { WithSerializer = true };
};

namespace UE::PersistentState
{
	struct PERSISTENTSTATE_API FWorldState
	{
		/** load constructor */
		explicit FWorldState(const FWorldStateDataHeader& InHeader)
			: Header(InHeader)
		{}
		/** save constructor, header should be fully filled before save is finished */
		explicit FWorldState(FName InWorld)
		{
			Header.WorldName = InWorld.ToString();
		}

		FWorldStateDataHeader Header;
		TArray<uint8> Data;

		FName GetWorld() const { return FName{Header.WorldName}; }
		TArray<uint8>& GetData() { return Data; }
		const TArray<uint8>& GetData() const { return Data; }
	};
}

using FWorldStateSharedRef = TSharedPtr<UE::PersistentState::FWorldState, ESPMode::ThreadSafe>;

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateSlotHeader
{
	GENERATED_BODY()

	static FPersistentStateSlotHeader InvalidHeader;
	static constexpr uint32 InvalidSize = TNumericLimits<uint32>::Max();

	FPersistentStateSlotHeader() = default;
	explicit FPersistentStateSlotHeader(int32 InHeaderTag)
		: SlotHeaderTag(InHeaderTag)
	{}
	
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	void ResetIntermediateData()
	{
		LastSavedWorld.Reset();
		WorldHeaderDataStart = InvalidSize;
		WorldHeaderDataCount = InvalidSize;
		Timestamp = {};
	}

	friend void operator<<(FArchive& Ar, FPersistentStateSlotHeader& Value)
	{
		Ar << Value.SlotHeaderTag;
		Ar << Value.SlotName;
		Ar << Value.Title;
		Ar << Value.Timestamp;
		Ar << Value.LastSavedWorld;
		Ar << Value.WorldHeaderDataStart;
		Ar << Value.WorldHeaderDataCount;
	}

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

	/** name of a last saved world */
	UPROPERTY()
	FString LastSavedWorld;
	
	/** offset from the beginning of the save file to the world header data */
	UPROPERTY()
	uint32 WorldHeaderDataStart = InvalidSize;

	/** number of worlds stored in the slot */
	UPROPERTY()
	uint32 WorldHeaderDataCount = InvalidSize;
};

template <>
struct TStructOpsTypeTraits<FPersistentStateSlotHeader>: public TStructOpsTypeTraitsBase2<FPersistentStateSlotHeader>
{
	enum { WithSerializer = true };
};


struct PERSISTENTSTATE_API FPersistentStateSlot
{
	FPersistentStateSlot() = default;
	/** create state slot from a loaded archive */
	FPersistentStateSlot(FArchive& Ar, const FString& InFilePath);
	/** create a state slot that is not yet associated with any actual data */
	FPersistentStateSlot(const FName& SlotName, const FText& Title);

	/**
	 * try to associate slot with a physical file
	 * if failed, persistent slot is reset, usual slot is deleted
	 */
	bool TrySetFilePath(FArchive& Ar, const FString& InFilePath);
	/**
	 * override file path. Should be called only when persistent slot is given a new file
	 * usual slots are removed if they're not associated with a valid file path
	 */
	void SetFilePath(const FString& InFilePath);
	/** reset all data */
	void ResetFileData();

	/** @return true if state slot has world state for a given world */
	bool HasWorldState(FName WorldName) const;
	/** load world state from a slot archive to a shared data ref */
	FWorldStateSharedRef LoadWorldState(FArchive& ReadArchive, FName WorldName) const;
	/** save world state to a slot archive */
	bool SaveWorldState(FWorldStateSharedRef NewWorldState, TFunction<TUniquePtr<FArchive>(const FString&)> CreateReadArchive, TFunction<TUniquePtr<FArchive>(const FString&)> CreateWriteArchive);
	/** @return name of the package world was stored initially */
	FString GetOriginalWorldPackage(FName WorldName) const;
	
	FORCEINLINE FArchive& operator<<(FArchive& Ar)
	{
		check(bValidBit);
		Ar << Header;
		return Ar;
	}

	FORCEINLINE bool IsValidSlot() const
	{
		return bValidBit;
	}

	FORCEINLINE FString GetFilePath() const
	{
		return FilePath;
	}

	FORCEINLINE bool HasFilePath() const
	{
		return !FilePath.IsEmpty();
	}
	
	FORCEINLINE FName GetSlotName() const
	{
		return FName{Header.SlotName};
	}
	
	FORCEINLINE FName GetWorldToLoad() const
	{
		return FName{Header.LastSavedWorld};
	}

private:

	int32 GetWorldHeaderIndex(FName WorldName) const;

	FORCEINLINE void SetLastSavedWorld(FName InWorldName)
	{
		Header.LastSavedWorld = InWorldName.ToString();
		UpdateTimestamp();
	}
	
	FORCEINLINE void UpdateTimestamp()
	{
		Header.Timestamp = FDateTime::Now();
	}
	
	void LoadWorldData(FArchive& ReadArchive, int32 HeaderIndex, TArray<uint8>& OutData) const;
	
	/** physical file path, can be empty for default and newly created slots */
	FString FilePath;
	
	/** slot header, stored as a part of physical file */
	FPersistentStateSlotHeader Header;
	
	/** list of world headers */
	TArray<FWorldStateDataHeader> WorldHeaders;

	/** valid bit that indicates whether state slot was loaded correctly. Always valid for slots without physical state */
	uint8 bValidBit: 1 = false;
};

using FPersistentStateSlotSharedRef = TSharedPtr<FPersistentStateSlot, ESPMode::ThreadSafe>;

