#pragma once

#include "CoreMinimal.h"
#include "Managers/PersistentStateManager.h"

#include "PersistentStateSlot.generated.h"

struct FPersistentStateSlotHandle;
class UPersistentStateSlotDescriptor;
class AActor;
class UActorComponent;
class USceneComponent;
class IPersistentStateObject;

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateFixedInteger
{
	GENERATED_BODY()

	FPersistentStateFixedInteger() = default;
	explicit constexpr FPersistentStateFixedInteger(int32 InTag)
		: Tag(InTag)
	{}
	FPersistentStateFixedInteger& operator=(int32 InTag)
	{
		Tag = InTag;
		return *this;
	}
	
	operator int32() const { return Tag; }

	bool Serialize(FArchive& Ar) { Ar << *this; return true; }
	bool Serialize(FStructuredArchive::FSlot Slot) { Slot << *this; return true; }
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateFixedInteger& Value);
	friend void operator<<(FStructuredArchive::FSlot Slot, FPersistentStateFixedInteger& Value);

	UPROPERTY()
	int32 Tag = 0;
};

template <>
struct TStructOpsTypeTraits<FPersistentStateFixedInteger>: public TStructOpsTypeTraitsBase2<FPersistentStateFixedInteger>
{
	enum
	{
		WithSerializer = true,
		WithStructuredSerializer = true,
	};
};

static constexpr FPersistentStateFixedInteger INVALID_SIZE{TNumericLimits<int32>::Max()};
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
	UPROPERTY()
	FSoftClassPath ChunkType;
	/** chunk length, excluding header size */
	UPROPERTY()
	FPersistentStateFixedInteger ChunkSize{0};

	FPersistentStateDataChunkHeader() = default;
	explicit FPersistentStateDataChunkHeader(const UClass* InChunkType, uint32 InChunkSize)
		: ChunkType(FSoftClassPath{InChunkType})
		, ChunkSize(InChunkSize)
	{}
	
	bool IsValid() const
	{
		return !ChunkType.IsNull();
	}

	bool IsEmpty() const
	{
		return !IsValid() || ChunkSize == 0;
	}
	
	bool Serialize(FStructuredArchive::FSlot Slot)
	{
		Slot << *this;
		return true;
	}
	
	friend void operator<<(FStructuredArchive::FSlot Slot, FPersistentStateDataChunkHeader& Value)
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		FArchive& Ar = Record.GetUnderlyingArchive();
		Value.ChunkType.SerializePath(Ar);
		
		// Record << SA_VALUE(TEXT("Type"), Value.ChunkType);
		Record << SA_VALUE(TEXT("Size"), Value.ChunkSize);
	}

	friend bool operator==(const FPersistentStateDataChunkHeader& A, const FPersistentStateDataChunkHeader& B)
	{
		return A.ChunkType == B.ChunkType && A.ChunkSize == B.ChunkSize;
	}
};

template <>
struct TStructOpsTypeTraits<FPersistentStateDataChunkHeader>: public TStructOpsTypeTraitsBase2<FPersistentStateDataChunkHeader>
{
	enum
	{
		WithStructuredSerializer = true,
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

	FORCEINLINE bool IsValid() const
	{
		return	ChunkCount != INVALID_SIZE &&
				ObjectTablePosition != INVALID_SIZE &&
				StringTablePosition != INVALID_SIZE &&
				DataSize != INVALID_SIZE;
	}
	
	PERSISTENTSTATE_API friend void operator<<(FStructuredArchive::FSlot Slot, FStateDataHeader& Value);
	PERSISTENTSTATE_API friend bool operator==(const FStateDataHeader& A, const FStateDataHeader& B);

	/** world header magic tag */
	UPROPERTY()
	FPersistentStateFixedInteger HeaderTag{INVALID_HEADER_TAG};

	/** number of managers stored as a part of the state data */
	UPROPERTY()
	uint32 ChunkCount = INVALID_SIZE;
	
	/** object table position inside the state data, absolute is calculated as DataStart + ObjectTablePosition. Can be zero */
	UPROPERTY()
	uint32 ObjectTablePosition = INVALID_SIZE;

	/** string table position inside the state data, absolute is calculated as DataStart + StringTablePosition. Can be zero */
	UPROPERTY()
	uint32 StringTablePosition = INVALID_SIZE;

	/** state data start position inside the slot save archive, never zero */
	UPROPERTY()
	FPersistentStateFixedInteger DataStart{INVALID_SIZE};

	/** state data length in bytes in the save file, including object table and string table, can be zero */
	UPROPERTY()
	uint32 DataSize = INVALID_SIZE;
};

USTRUCT()
struct PERSISTENTSTATE_API FGameStateDataHeader: public FStateDataHeader
{
	GENERATED_BODY()

	FGameStateDataHeader()
		: FStateDataHeader(GAME_HEADER_TAG)
	{}

	FORCEINLINE bool IsValid() const
	{
		return HeaderTag == GAME_HEADER_TAG && Super::IsValid();
	}
};

USTRUCT()
struct PERSISTENTSTATE_API FWorldStateDataHeader: public FStateDataHeader
{
	GENERATED_BODY()

	FWorldStateDataHeader()
		: Super(WORLD_HEADER_TAG)
	{}
	
	FORCEINLINE bool IsValid() const
	{
		return	HeaderTag == WORLD_HEADER_TAG &&
				!World.IsEmpty() &&
				!WorldPackage.IsEmpty() &&
				Super::IsValid();
	}

	FORCEINLINE FName GetWorld() const
	{
		return FName{World};
	}
	
	PERSISTENTSTATE_API friend void operator<<(FStructuredArchive::FSlot Slot, FWorldStateDataHeader& Value);
	PERSISTENTSTATE_API friend bool operator==(const FWorldStateDataHeader& A, const FWorldStateDataHeader& B);

	/** world name that uniquely identifies it in the save file */
	UPROPERTY()
	FString World;

	/** world package name */
	UPROPERTY()
	FString WorldPackage;
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
template <typename TDataHeader>
struct PERSISTENTSTATE_API FManagerState
{
	/** create manager state for save */
	static FManagerState<TDataHeader> CreateSaveState()
	{
		return FManagerState<TDataHeader>{};
	}

	/** create manager state for load */
	static FManagerState<TDataHeader> CreateLoadState(const TDataHeader& InHeader)
	{
		return FManagerState<TDataHeader>{InHeader};
	}

	uint32 GetAllocatedSize() const { return sizeof(TDataHeader) + Buffer.GetAllocatedSize(); }
	TArray<uint8>& GetData() { return Buffer; }
	const TArray<uint8>& GetData() const { return Buffer; }
		
	TDataHeader Header;
	TArray<uint8> Buffer;
	
private:
	/** save constructor */
	FManagerState()
	{
		Header.InitializeToEmpty();
	}
	/** load constructor */
	explicit FManagerState(const TDataHeader& InHeader)
		: Header(InHeader)
	{}
};

using FGameState	= FManagerState<FGameStateDataHeader>;
using FWorldState	= FManagerState<FWorldStateDataHeader>;
using FGameStateSharedRef	= TSharedPtr<FGameState, ESPMode::ThreadSafe>;
using FWorldStateSharedRef	= TSharedPtr<FWorldState, ESPMode::ThreadSafe>;
using FArchiveFactory = TFunction<TUniquePtr<FArchive>(const FString&)>;

struct FPersistentStateSlotSaveRequest
{
	bool IsValid() const;

	/** descriptor header, never empty */
	FPersistentStateDataChunkHeader DescriptorHeader;
	/** descriptor property information, never empty @todo fix copy on each invocation */
	FPersistentStatePropertyBunch DescriptorBunch;
	/** game state, almost certainly not null */
	FGameStateSharedRef GameState;
	/** world state, may be null */
	FWorldStateSharedRef WorldState;
};

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateSlot
{
	GENERATED_BODY()
public:
	FPersistentStateSlot() = default;
	/** create state slot from a loaded archive */
	FPersistentStateSlot(FArchive& Ar, const FString& InFilePath);
	/** create a state slot that is not yet associated with any actual data */
	FPersistentStateSlot(FName InSlotName, const FText& InTitle, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass);

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
	FWorldStateSharedRef LoadWorldState(FName World, FArchiveFactory CreateReadArchive) const;
	/** save state directly to the */
	void SaveStateDirect(const FPersistentStateSlotSaveRequest& Request, FArchiveFactory CreateWriteArchive);
	/** save new state to a slot archive */
	void SaveState(const FPersistentStateSlot& SourceSlot,
		const FPersistentStateSlotSaveRequest& Request,
		FArchiveFactory CreateReadArchive,
		FArchiveFactory CreateWriteArchive
	);
	
	/**
	 * Create descriptor bunch based on state slot desired descriptor class
	 * @OutDescriptorHeader
	 * @OutDescriptorBunch 
	 */
	static FPersistentStateSlotSaveRequest CreateSaveRequest(
		UWorld* World, const FPersistentStateSlot& StateSlot, const FPersistentStateSlotHandle& SlotHandle,
		const FGameStateSharedRef& GameState = {}, const FWorldStateSharedRef& WorldState = {}
	);

	/**
	 * create and @return descriptor from based on state slot descriptor class and serialized property bunch
	 * Works even if descriptor wasn't yet saved (slot is new or a default named slot)
	 * descriptor is transient e.g.
	 */
	static UPersistentStateSlotDescriptor* CreateSerializedDescriptor(UWorld* World, const FPersistentStateSlot& StateSlot, const FPersistentStateSlotHandle& SlotHandle);

	void GetSavedWorlds(TArray<FName>& OutStoredWorlds) const;
	
	uint32	GetAllocatedSize() const;
	FORCEINLINE bool	IsValidSlot() const { return bValidSlot; }
	FORCEINLINE FName	GetSlotName() const { return FName{SlotName}; }
	FORCEINLINE FText	GetSlotTitle() const { return SlotTitle; }
	FORCEINLINE FString GetFilePath() const { return FilePath; }
	FORCEINLINE bool	HasFilePath() const { return !FilePath.IsEmpty(); }
	FORCEINLINE FDateTime	GetTimeStamp() const { return TimeStamp; }
	FORCEINLINE FString		GetLastSavedWorld() const { return LastSavedWorld; }

	PERSISTENTSTATE_API friend bool operator==(const FPersistentStateSlot& A, const FPersistentStateSlot& B);
private:

	UClass* ResolveDescriptorClass() const;
	bool IsPhysical() const;
	
	/** save new state */
	void SaveStateToArchive(const FPersistentStateSlotSaveRequest& Request, FArchiveFactory CreateWriteArchive, TArray<uint8>* PersistentData = nullptr);

	/**
	 * Read data from an archive into a data buffer, taking into account possible decompression
	 * If compression was enabled during @WriteCompressed operation data is uncompressed first before being written into a
	 * buffer
	 * @param Ar loading archive
	 * @param DataStart start of the data chunk that should be read into a buffer
	 * @param DataSize size of data that should be read into a buffer
	 * @param OutBuffer result
	 */
	static void ReadCompressed(FArchive& Ar, int32 DataStart, int32 DataSize, TArray<uint8>& OutBuffer);

	/**
	 * Write data from the data buffer into an archive with possible compression as an intermediate step
	 * If compression is enabled (via WITH_STATE_DATA_COMPRESSION) and data is large enough,
	 * data buffer is compressed into the intermediate storage first before being written to the archive
	 * @param Ar writing archive
	 * @param Buffer data buffer to write into the archive
	 */
	static void WriteCompressed(FArchive& Ar, TArray<uint8>& Buffer);

	/** match @WorldName to index inside @WorldHeaders array */
	int32 GetWorldHeaderIndex(FName WorldName) const;
	
	/** physical file path, can be empty for default and newly created slots */
	FString FilePath;
	
	/** Logical save slot name */
	UPROPERTY()
    FString SlotName;

	/** User-defined slot title */
	UPROPERTY()
	FText SlotTitle;

	/** last saved world, if any */
	UPROPERTY()
	FString LastSavedWorld;

	/** last save timestamp */
	UPROPERTY()
	FDateTime TimeStamp;
	
	/** descriptor data start */
	UPROPERTY()
	FPersistentStateFixedInteger DescriptorDataStart;
	
	/** descriptor header */
	UPROPERTY()
	FPersistentStateDataChunkHeader DescriptorHeader;

	/** descriptor property data */
	UPROPERTY()
	FPersistentStatePropertyBunch DescriptorBunch;

	/** game header */
	UPROPERTY()
	FGameStateDataHeader GameHeader;
	
	/** list of world headers */
	UPROPERTY()
	TArray<FWorldStateDataHeader> WorldHeaders;

	/** valid bit that indicates whether state slot was loaded correctly. Always valid for slots without physical state */
	uint8 bValidSlot: 1 = false;
};

using FPersistentStateSlotSharedRef = TSharedPtr<FPersistentStateSlot, ESPMode::ThreadSafe>;
using FPersistentStateSlotWeakRef	= TWeakPtr<FPersistentStateSlot, ESPMode::ThreadSafe>;
