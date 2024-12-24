#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

#include "PersistentStateSlot.generated.h"

class AActor;
class UActorComponent;
class USceneComponent;
class IPersistentStateObject;

static constexpr uint32 INVALID_SIZE = TNumericLimits<uint32>::Max();
static constexpr int32 INVALID_HEADER_TAG	= 0x00000000;
static constexpr int32 SLOT_HEADER_TAG		= 0x53A41B6D;
static constexpr int32 GAME_HEADER_TAG		= 0x8D4525F3;
static constexpr int32 WORLD_HEADER_TAG		= 0x3AEF241C;

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
struct PERSISTENTSTATE_API FStateDataHeader
{
	GENERATED_BODY()

	FStateDataHeader() = default;
	FStateDataHeader(uint32 InHeaderTag)
		: HeaderTag(InHeaderTag)
	{}
	
	void InitializeToEmpty()
	{
		ChunkCount = ObjectTablePosition = StringTablePosition = 0;
		DataStart = DataSize = 0;
	}

	void CheckValid() const
	{
		check(ChunkCount != INVALID_SIZE);
		check(ObjectTablePosition != INVALID_SIZE);
		check(StringTablePosition != INVALID_SIZE);
		check(DataSize != INVALID_SIZE);
	}
	
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend void operator<<(FArchive& Ar, FStateDataHeader& Value)
	{
		Ar << Value.HeaderTag;
		Ar << Value.ChunkCount;
		Ar << Value.ObjectTablePosition;
		Ar << Value.StringTablePosition;
		Ar << Value.DataStart;
		Ar << Value.DataSize;
	}

	/** world header magic tag */
	UPROPERTY()
	uint32 HeaderTag = INVALID_HEADER_TAG;

	/** number of managers stored as a part of the state data */
	UPROPERTY()
	uint32 ChunkCount = INVALID_SIZE;
	
	/** object table position inside the state data, can be zero */
	UPROPERTY()
	uint32 ObjectTablePosition = INVALID_SIZE;

	/** string table position inside the state data, can be zero */
	UPROPERTY()
	uint32 StringTablePosition = INVALID_SIZE;

	/** state data start position inside the slot save archive, never zero */
	uint32 DataStart = INVALID_SIZE;

	/** state data length in bytes in the save file, including object table and string table, can be zero */
	UPROPERTY()
	uint32 DataSize = INVALID_SIZE;
};

template <>
struct TStructOpsTypeTraits<FStateDataHeader>: public TStructOpsTypeTraitsBase2<FStateDataHeader>
{
	enum
	{
		WithSerializer = true
	};
};

USTRUCT()
struct PERSISTENTSTATE_API FGameStateDataHeader: public FStateDataHeader
{
	GENERATED_BODY()

	FGameStateDataHeader()
		: FStateDataHeader(GAME_HEADER_TAG)
	{}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
};

template <>
struct TStructOpsTypeTraits<FGameStateDataHeader>: public TStructOpsTypeTraitsBase2<FGameStateDataHeader>
{
	enum { WithSerializer = true };
};

USTRUCT()
struct PERSISTENTSTATE_API FWorldStateDataHeader: public FStateDataHeader
{
	GENERATED_BODY()

	FWorldStateDataHeader()
		: FStateDataHeader(WORLD_HEADER_TAG)
	{}
	
	void CheckValid() const
	{
		Super::CheckValid();
		
		check(!WorldName.IsEmpty());
		check(!WorldPackageName.IsEmpty());
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	friend void operator<<(FArchive& Ar, FWorldStateDataHeader& Value)
	{
		static_cast<FStateDataHeader>(Value).Serialize(Ar);
		Ar << Value.WorldName;
		Ar << Value.WorldPackageName;
	}
	
	/** world name that uniquely identifies it in the save file */
	UPROPERTY()
	FString WorldName;

	/** world package name */
	UPROPERTY()
	FString WorldPackageName;
};

template <>
struct TStructOpsTypeTraits<FWorldStateDataHeader>: public TStructOpsTypeTraitsBase2<FWorldStateDataHeader>
{
	enum { WithSerializer = true };
};

namespace UE::PersistentState
{
	struct PERSISTENTSTATE_API FGameState
	{
		/** save constructor */
		FGameState()
		{
			Header.InitializeToEmpty();
		}
		/** load constructor */
		explicit FGameState(const FGameStateDataHeader& InHeader)
			: Header(InHeader)
		{}

		uint32 GetAllocatedSize() const { return sizeof(FGameState) + Data.GetAllocatedSize(); }
		TArray<uint8>& GetData() { return Data; }
		const TArray<uint8>& GetData() const { return Data; }
		
		FGameStateDataHeader Header;
		TArray<uint8> Data;
	};

	/**
	 * +------------------------+
	 * | World State Header		|
	 * +------------------------+
	 * | Chunk Header			|
	 * | Chunk Data				|
	 * +------------------------+
	 * | Chunk Header			|
	 * | Chunk Data				|
	 * +------------------------+
	 * ...						|
	 * +------------------------+
	 * | </End Tag>				|
	 * +------------------------+
	 */
	struct PERSISTENTSTATE_API FWorldState
	{
		/** save constructor, header is gradually filled before save is finished */
		FWorldState()
		{
			Header.InitializeToEmpty();
		}
		/** load constructor */
		explicit FWorldState(const FWorldStateDataHeader& InHeader)
			: Header(InHeader)
		{}

		uint32 GetAllocatedSize() const { return sizeof(FWorldState) + Data.GetAllocatedSize(); }
		uint32 GetNum() const { return Data.Num(); }

		FName GetWorld() const { return FName{Header.WorldName}; }
		TArray<uint8>& GetData() { return Data; }
		const TArray<uint8>& GetData() const { return Data; }
		
		FWorldStateDataHeader Header;
		TArray<uint8> Data;
	};
}

using FGameStateSharedRef	= TSharedPtr<UE::PersistentState::FGameState, ESPMode::ThreadSafe>;
using FWorldStateSharedRef	= TSharedPtr<UE::PersistentState::FWorldState, ESPMode::ThreadSafe>;

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateSlotHeader
{
	GENERATED_BODY()

	FPersistentStateSlotHeader() = default;
	
	void Initialize(const FString& InSlotName, const FText& InTitle)
	{
		SlotName = InSlotName;
		Title = InTitle;
	}
	
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	void ResetIntermediateData()
	{
		LastSavedWorld.Reset();
		HeaderDataCount = INVALID_SIZE;
		Timestamp = {};
	}

	friend void operator<<(FArchive& Ar, FPersistentStateSlotHeader& Value)
	{
		Ar << Value.SlotName;
		Ar << Value.Title;
		Ar << Value.Timestamp;
		Ar << Value.LastSavedWorld;
		Ar << Value.HeaderDataCount;
	}
	
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

	/** number of headers stored in the slot, game header + world headers */
	UPROPERTY()
	uint32 HeaderDataCount = INVALID_SIZE;
};

template <>
struct TStructOpsTypeTraits<FPersistentStateSlotHeader>: public TStructOpsTypeTraitsBase2<FPersistentStateSlotHeader>
{
	enum { WithSerializer = true };
};

using FArchiveFactory = TFunction<TUniquePtr<FArchive>(const FString&)>;
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
	/** load game state to a shared game data via archive reader */
	FGameStateSharedRef LoadGameState(FArchiveFactory CreateReadArchive) const;
	/** load world state to a shared world data via archive reader */
	FWorldStateSharedRef LoadWorldState(FName WorldName, FArchiveFactory CreateReadArchive) const;
	/** save new state to a slot archive */
	bool SaveState(const FPersistentStateSlot& SourceSlot,
		FGameStateSharedRef NewGameState, FWorldStateSharedRef NewWorldState,
		FArchiveFactory CreateReadArchive,
		FArchiveFactory CreateWriteArchive
	);
	/** @return name of the package world was stored initially */
	FString GetOriginalWorldPackage(FName WorldName) const;

	FORCEINLINE uint32 GetAllocatedSize() const { return WorldHeaders.GetAllocatedSize(); }
	
	FORCEINLINE FArchive& operator<<(FArchive& Ar)
	{
		check(bValidBit);
		Ar << SlotHeader;
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
		return FName{SlotHeader.SlotName};
	}
	
	FORCEINLINE FName GetWorldToLoad() const
	{
		return FName{SlotHeader.LastSavedWorld};
	}

private:

	static void LoadWorldData(const FWorldStateDataHeader& Header, FArchive& Reader, TArray<uint8>& OutData);
	int32 GetWorldHeaderIndex(FName WorldName) const;

	FORCEINLINE void SetLastSavedWorld(FName InWorldName)
	{
		SlotHeader.LastSavedWorld = InWorldName.ToString();
		UpdateTimestamp();
	}
	
	FORCEINLINE void UpdateTimestamp()
	{
		SlotHeader.Timestamp = FDateTime::Now();
	}
	
	/** physical file path, can be empty for default and newly created slots */
	FString FilePath;
	
	/** slot header, stored as a part of physical file */
	FPersistentStateSlotHeader SlotHeader;

	/** game header */
	FGameStateDataHeader GameHeader;
	
	/** list of world headers */
	TArray<FWorldStateDataHeader> WorldHeaders;

	/** valid bit that indicates whether state slot was loaded correctly. Always valid for slots without physical state */
	uint8 bValidBit: 1 = false;
};

using FPersistentStateSlotSharedRef = TSharedPtr<FPersistentStateSlot, ESPMode::ThreadSafe>;

