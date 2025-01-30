#include "PersistentStateSlot.h"

#include "PersistentStateModule.h"
#include "PersistentStateSerialization.h"
#include "PersistentStateSlotDescriptor.h"
#include "PersistentStateStatics.h"
#include "Algo/AllOf.h"
#include "Compression/OodleDataCompressionUtil.h"

FArchive& operator<<(FArchive& Ar, FPersistentStateFixedInteger& Value)
{
	Ar.Serialize(&Value.Tag, sizeof(Value.Tag));
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FPersistentStateFixedInteger& Value)
{
	Slot.Serialize(&Value.Tag, sizeof(Value.Tag));
}

void operator<<(FStructuredArchive::FSlot Slot, FStateDataHeader& Value)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("Tag"), Value.HeaderTag);
	Record << SA_VALUE(TEXT("ChunkCount"), Value.ChunkCount);
	Record << SA_VALUE(TEXT("ObjectTablePosition"), Value.ObjectTablePosition);
	Record << SA_VALUE(TEXT("StringTablePosition"), Value.StringTablePosition);
	Record << SA_VALUE(TEXT("DataStart"), Value.DataStart);
	Record << SA_VALUE(TEXT("DataSize"), Value.DataSize);
}

bool operator==(const FStateDataHeader& A, const FStateDataHeader& B)
{
	return	A.HeaderTag == B.HeaderTag && A.ChunkCount == B.ChunkCount &&
			A.ObjectTablePosition == B.ObjectTablePosition &&
			A.StringTablePosition == B.StringTablePosition &&
			A.DataStart == B.DataStart && A.DataSize == B.DataSize;
}

void operator<<(FStructuredArchive::FSlot Slot, FWorldStateDataHeader& Value)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("Tag"), Value.HeaderTag);
	Record << SA_VALUE(TEXT("ChunkCount"), Value.ChunkCount);
	Record << SA_VALUE(TEXT("ObjectTablePosition"), Value.ObjectTablePosition);
	Record << SA_VALUE(TEXT("StringTablePosition"), Value.StringTablePosition);
	Record << SA_VALUE(TEXT("DataStart"), Value.DataStart);
	Record << SA_VALUE(TEXT("DataSize"), Value.DataSize);
	Record << SA_VALUE(TEXT("World"), Value.World);
	Record << SA_VALUE(TEXT("WorldPackage"), Value.WorldPackage);
}

bool operator==(const FWorldStateDataHeader& A, const FWorldStateDataHeader& B)
{
	return	A.World == B.World && A.WorldPackage == B.WorldPackage &&
			static_cast<FStateDataHeader>(A) == static_cast<FStateDataHeader>(B);
}

bool FPersistentStateSlotSaveRequest::IsValid() const
{
	return	!DescriptorHeader.IsEmpty() && !DescriptorBunch.IsEmpty()
		&& (!GameState.IsValid() || GameState->Header.IsValid())
		&& (!WorldState.IsValid() || WorldState->Header.IsValid());
}

bool operator==(const FPersistentStateSlot& A, const FPersistentStateSlot& B)
{
	return	A.SlotName == B.SlotName &&
			A.SlotTitle.ToString() == B.SlotTitle.ToString() &&
			A.LastSavedWorld == B.LastSavedWorld &&
			A.TimeStamp == B.TimeStamp &&
			A.DescriptorDataStart == B.DescriptorDataStart &&
			A.DescriptorHeader == B.DescriptorHeader &&
			A.DescriptorBunch == B.DescriptorBunch &&
			A.GameHeader == B.GameHeader &&
			A.WorldHeaders == B.WorldHeaders;
}

FPersistentStateSlot::FPersistentStateSlot(FArchive& Ar, const FString& InFilePath)
{
	TrySetFilePath(Ar, InFilePath);
}

FPersistentStateSlot::FPersistentStateSlot(FName InSlotName, const FText& InSlotTitle, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass)
	: SlotName(InSlotName.ToString())
	, SlotTitle(InSlotTitle)
	, DescriptorHeader{DescriptorClass, 0}
{
	GameHeader.InitializeToEmpty();
	bValidSlot = true;
}

bool FPersistentStateSlot::IsPhysical() const
{
	return !SlotName.IsEmpty() && !DescriptorHeader.IsEmpty() && GameHeader.IsValid()
			&& Algo::AllOf(WorldHeaders, [](const auto& Header) { return Header.IsValid(); });
}

bool FPersistentStateSlot::TrySetFilePath(FArchive& Ar, const FString& InFilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(HasFilePath() == false);
	check(Ar.IsLoading() && Ar.Tell() == 0);

	FPersistentStateSaveGameArchive SaveGameArchive{Ar};
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateLoadFormatter(SaveGameArchive);
	FStructuredArchive StructuredArchive{*Formatter};
	FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();

	FPersistentStateFixedInteger HeaderTag{INVALID_HEADER_TAG};
	RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
	if (HeaderTag != SLOT_HEADER_TAG)
	{
		return false;
	}
	
	FPersistentStateSlot TempSlot{};
	StaticStruct()->SerializeItem(RootRecord.EnterField(TEXT("StateSlot")), &TempSlot, nullptr);

	if (!TempSlot.IsPhysical())
	{
		return false;
	}

	*this = TempSlot;
	FilePath = InFilePath;
    // Rename state slot based if filename is different from the slot name stored in the file
    SlotName = FPaths::GetBaseFilename(FilePath);
	// ValidBit is false by default, so we should make it true again
	bValidSlot = true;
	
	return true;
}

void FPersistentStateSlot::SetFilePath(const FString& InFilePath)
{
	FilePath = InFilePath;
}

void FPersistentStateSlot::ResetFileData()
{
	FilePath.Reset();
	bValidSlot = true;
}

UClass* FPersistentStateSlot::ResolveDescriptorClass() const
{
	UClass* DescriptorClass = DescriptorHeader.ChunkType.ResolveClass();
	if (DescriptorClass == nullptr)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: descriptor class is not loaded beforehand."), *DescriptorHeader.ChunkType.ToString());
		DescriptorClass = DescriptorHeader.ChunkType.TryLoadClass<UPersistentStateSlotDescriptor>();
	}

	check(DescriptorClass);
	return DescriptorClass;
}

FPersistentStateSlotSaveRequest FPersistentStateSlot::CreateSaveRequest(UWorld* World, const FPersistentStateSlot& StateSlot, const FPersistentStateSlotHandle& SlotHandle, const FGameStateSharedRef& GameState, const FWorldStateSharedRef& WorldState)
{
	check(IsInGameThread());
	check(StateSlot.DescriptorHeader.IsValid());
	UClass* DescriptorClass = StateSlot.ResolveDescriptorClass();
	check(World && DescriptorClass);
	
	UPersistentStateSlotDescriptor* Descriptor = NewObject<UPersistentStateSlotDescriptor>(GetTransientPackage(), DescriptorClass);
	Descriptor->SaveDescriptor(World, SlotHandle);

	FPersistentStateSlotSaveRequest Request{};
	UE::PersistentState::SaveObject(*Descriptor, Request.DescriptorBunch, false);
	Request.DescriptorHeader = FPersistentStateDataChunkHeader(DescriptorClass, Request.DescriptorBunch.Num());
	Request.GameState = GameState;
	Request.WorldState = WorldState;

	return Request;
}

UPersistentStateSlotDescriptor* FPersistentStateSlot::CreateSerializedDescriptor(UWorld* World, const FPersistentStateSlot& StateSlot, const FPersistentStateSlotHandle& SlotHandle)
{
	check(StateSlot.DescriptorHeader.IsValid());
	UClass* DescriptorClass = StateSlot.ResolveDescriptorClass();
	check(World && DescriptorClass);

	UPersistentStateSlotDescriptor* Descriptor = NewObject<UPersistentStateSlotDescriptor>(GetTransientPackage(), DescriptorClass);
	if (!StateSlot.DescriptorHeader.IsEmpty())
	{
		UE::PersistentState::LoadObject(*Descriptor, StateSlot.DescriptorBunch, false);
	}
	
	Descriptor->LoadDescriptor(World, SlotHandle, FPersistentStateSlotDesc{StateSlot});

	return Descriptor;
}

void FPersistentStateSlot::GetSavedWorlds(TArray<FName>& OutStoredWorlds) const
{
	OutStoredWorlds.Reset();
	for (const FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		OutStoredWorlds.Add(FName{WorldHeader.World});
	}
}

int32 FPersistentStateSlot::GetWorldHeaderIndex(FName WorldName) const
{
	return WorldHeaders.IndexOfByPredicate([&WorldName](const FWorldStateDataHeader& Header)
	{
		return Header.World == WorldName;
	});
}

bool FPersistentStateSlot::HasWorldState(FName WorldName) const
{
	return WorldHeaders.IsValidIndex(GetWorldHeaderIndex(WorldName));
}

FGameStateSharedRef FPersistentStateSlot::LoadGameState(FArchiveFactory CreateReadArchive) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	// verify that slot is associated with file path
	check(HasFilePath());

	FGameStateSharedRef Result = MakeShared<FGameState>(FGameState::CreateLoadState(GameHeader));
	
	if (GameHeader.DataStart > 0)
	{
		TUniquePtr<FArchive> Reader = CreateReadArchive(FilePath);
		check(Reader && Reader->IsLoading());

		FPersistentStateSaveGameArchive SaveGameArchive{*Reader};
		ReadCompressed(SaveGameArchive, GameHeader.DataStart, GameHeader.DataSize, Result->Buffer);
	}
	
	return Result;
}

void FPersistentStateSlot::SaveStateDirect(const FPersistentStateSlotSaveRequest& Request, FArchiveFactory CreateWriteArchive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	// verify that slot is associated with file path
	check(bValidSlot && HasFilePath());

	// reset world header information
	WorldHeaders.Empty();

	SaveStateToArchive(Request, CreateWriteArchive, nullptr);
}

void FPersistentStateSlot::SaveState(const FPersistentStateSlot& SourceSlot, const FPersistentStateSlotSaveRequest& Request, FArchiveFactory CreateReadArchive, FArchiveFactory CreateWriteArchive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	// verify that slot is associated with file path
	check(bValidSlot && HasFilePath());
	check(Request.IsValid());

	// copy world header data from the source slot
	WorldHeaders = SourceSlot.WorldHeaders;
	if (Request.WorldState.IsValid())
	{
		// remove old header data for the world, unless it is a new world
		if (const int32 HeaderIndex = GetWorldHeaderIndex(Request.WorldState->Header.GetWorld()); WorldHeaders.IsValidIndex(HeaderIndex))
		{
			WorldHeaders.RemoveAtSwap(HeaderIndex);
		}
	}
	
	// read world data that not going to change during the save operation
	TArray<uint8> PersistentData;
	if (!WorldHeaders.IsEmpty())
	{
		// sort world headers by DataStart, so that access to data reader is mostly sequential
		Algo::Sort(WorldHeaders, [](const FWorldStateDataHeader& A, const FWorldStateDataHeader& B)
		{
			return A.DataStart < B.DataStart;
		});

		int32 PersistentDataSize = 0;
		for (const FWorldStateDataHeader& WorldHeader: WorldHeaders)
		{
			PersistentDataSize += WorldHeader.DataSize;
		}
		
		PersistentData.AddZeroed(PersistentDataSize);
		// read data for other worlds from the source slot
		TUniquePtr<FArchive> Reader = CreateReadArchive(SourceSlot.FilePath);
		check(Reader.IsValid() && Reader->IsLoading());
		FPersistentStateSaveGameArchive SaveGameArchive{*Reader};

		uint8* PersistentDataPtr = PersistentData.GetData();
		for (const FWorldStateDataHeader& Header: WorldHeaders)
		{
			check(Header.IsValid());
			
			SaveGameArchive.Seek(Header.DataStart);
			SaveGameArchive.Serialize(PersistentDataPtr, Header.DataSize);
			PersistentDataPtr += Header.DataSize;
		}
	}

	SaveStateToArchive(Request, CreateWriteArchive, &PersistentData);
}

uint32 FPersistentStateSlot::GetAllocatedSize() const
{
	uint32 TotalSize = 0;
	TotalSize += WorldHeaders.GetAllocatedSize();
	TotalSize += DescriptorBunch.GetAllocatedSize();
	
	return TotalSize;
}

void FPersistentStateSlot::SaveStateToArchive(const FPersistentStateSlotSaveRequest& Request, FArchiveFactory CreateWriteArchive, TArray<uint8>* PersistentData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	// update timestamp
	TimeStamp = FDateTime::Now();
	
	// update descriptor data
	DescriptorHeader = Request.DescriptorHeader;
	DescriptorBunch = Request.DescriptorBunch;

	// update headers
	GameHeader.InitializeToEmpty();
	if (Request.GameState.IsValid())
	{
		GameHeader = Request.GameState->Header;
	}
	
	if (Request.WorldState.IsValid())
	{
		WorldHeaders.Insert(Request.WorldState->Header, 0);
		// update last saved world
		LastSavedWorld = Request.WorldState->Header.GetWorld().ToString();
	}
	
	TUniquePtr<FArchive> Writer = CreateWriteArchive(FilePath);
	check(Writer.IsValid());
		
	FPersistentStateSaveGameArchive SaveGameArchive{*Writer};
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateSaveFormatter(SaveGameArchive);
	FStructuredArchive StructuredArchive{*Formatter};
	FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();
	
	const int32 SlotHeaderTagStart = SaveGameArchive.Tell();
	{
		FPersistentStateFixedInteger HeaderTag{INVALID_HEADER_TAG};
		// write invalid header tag to identify corrupted save file in case game crashes mid save
		RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
	}
	const int32 SlotHeaderTagEnd = SaveGameArchive.Tell();

	// save state slot
	const int32 StateSlotDataStart = SaveGameArchive.Tell();
	StaticStruct()->SerializeItem(RootRecord.EnterField(TEXT("StateSlot")), this, nullptr);
	const int32 StateSlotDataEnd = SaveGameArchive.Tell();

	// mark a descriptor data start
	DescriptorDataStart = StateSlotDataEnd;

	// save new game state
	GameHeader.DataStart = SaveGameArchive.Tell();
	if (Request.GameState.IsValid())
	{
		check(GameHeader.DataSize == Request.GameState->Buffer.Num());
		WriteCompressed(SaveGameArchive, Request.GameState->Buffer);
	}

	// save new world state, stored as a first world header
	if (Request.WorldState.IsValid())
	{
		// save new world state
		WorldHeaders[0].DataStart = SaveGameArchive.Tell();
		check(WorldHeaders[0].DataSize == Request.WorldState->Buffer.Num());
		WriteCompressed(SaveGameArchive, Request.WorldState->Buffer);
	}
	
	if (PersistentData != nullptr)
	{
		const int32 StartIndex = Request.WorldState.IsValid() ? 1 : 0;
		// Save rest of the worlds. Write old world data, in the same order as it was read.
		// DataSize is the same, DataStart is different.
		uint8* PersistentDataPtr = PersistentData->GetData();
		for (int32 Index = StartIndex; Index < WorldHeaders.Num(); ++Index)
		{
			WorldHeaders[Index].DataStart = SaveGameArchive.Tell();
			// serialize directly, as data that was read from a source state slot is already in a final state (compressed or not)
			SaveGameArchive.Serialize(PersistentDataPtr, WorldHeaders[Index].DataSize);

			PersistentDataPtr += WorldHeaders[Index].DataSize;
		}
	}

	// do not re-write header tag if saving with a debug formatter, because we can't safely backtrack with json/xml formatters
	// xml does not write anything to the archive until formatter is destroyed, json is simply scuffed
	// debug formatters are not meant to be read back
	if (FPersistentStateFormatter::IsReleaseFormatter())
	{
		// seek to the header start and re-write game and world headers
		SaveGameArchive.Seek(StateSlotDataStart);
		StaticStruct()->SerializeItem(RootRecord.EnterField(TEXT("StateSlot")), this, nullptr);
		check(SaveGameArchive.Tell() == StateSlotDataEnd);
		
		// seek to the start and re-write slot header tag
		SaveGameArchive.Seek(SlotHeaderTagStart);
		{
			FPersistentStateFixedInteger HeaderTag{SLOT_HEADER_TAG};
			RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
		}
		check(SaveGameArchive.Tell() == SlotHeaderTagEnd);
	}
}

FWorldStateSharedRef FPersistentStateSlot::LoadWorldState(FName World, FArchiveFactory CreateReadArchive) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	// verify that slot is associated with file path
	check(HasFilePath());
	check(World != NAME_None);
	
	const int32 HeaderIndex = GetWorldHeaderIndex(World);
	if (!WorldHeaders.IsValidIndex(HeaderIndex))
	{
		// no world data to load. This is OK
		UE_LOG(LogPersistentState, Error, TEXT("%s: Not found world data for world %s in state slot %s. Call HasWorldState beforehand"), *FString(__FUNCTION__), *World.ToString(), *SlotName);
		return {};
	}

	FWorldStateSharedRef Result = MakeShared<FWorldState>(FWorldState::CreateLoadState(WorldHeaders[HeaderIndex]));
	if (const FWorldStateDataHeader& Header = WorldHeaders[HeaderIndex]; Header.DataSize > 0)
	{
		TUniquePtr<FArchive> Reader = CreateReadArchive(FilePath);
		check(Reader && Reader->IsLoading());

		FPersistentStateSaveGameArchive SaveGameArchive{*Reader};
		ReadCompressed(SaveGameArchive, Header.DataStart, Header.DataSize, Result->Buffer);
	}
	
	return Result;
}

void FPersistentStateSlot::ReadCompressed(FArchive& Ar, int32 DataStart, int32 DataSize, TArray<uint8>& OutBuffer)
{
	check(Ar.IsLoading());
	check(OutBuffer.IsEmpty());
	
	Ar.Seek(DataStart);
	if (WITH_STATE_DATA_COMPRESSION)
	{
		TArray<uint8> CompressedData;
		CompressedData.SetNumZeroed(DataSize);
		
		Ar.Serialize(CompressedData.GetData(), DataSize);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FPersistentStateSlot_ReadCompressed, PersistentStateChannel);
			FOodleCompressedArray::DecompressToTArray(OutBuffer, CompressedData);
		}
	}
	else
	{
		OutBuffer.SetNumZeroed(DataSize);
		Ar.Serialize(OutBuffer.GetData(), DataSize);
	}
}

void FPersistentStateSlot::WriteCompressed(FArchive& Ar, TArray<uint8>& Buffer)
{
	check(Ar.IsSaving());
	if (WITH_STATE_DATA_COMPRESSION)
	{
		TArray<uint8> CompressedData;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FPersistentStateSlot_CompressState, PersistentStateChannel);
			FOodleCompressedArray::CompressTArray(CompressedData, Buffer, FOodleDataCompression::ECompressor::Kraken, FOodleDataCompression::ECompressionLevel::HyperFast1);
		}
		Ar.Serialize(CompressedData.GetData(), Buffer.Num());
	}
	else
	{
		Ar.Serialize(Buffer.GetData(), Buffer.Num());
	}
}
