#include "PersistentStateSlot.h"

#include "PersistentStateModule.h"
#include "PersistentStateSerialization.h"
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

void operator<<(FStructuredArchive::FSlot Slot, FWorldStateDataHeader& Value)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("Tag"), Value.HeaderTag);
	Record << SA_VALUE(TEXT("ChunkCount"), Value.ChunkCount);
	Record << SA_VALUE(TEXT("ObjectTablePosition"), Value.ObjectTablePosition);
	Record << SA_VALUE(TEXT("StringTablePosition"), Value.StringTablePosition);
	Record << SA_VALUE(TEXT("DataStart"), Value.DataStart);
	Record << SA_VALUE(TEXT("DataSize"), Value.DataSize);
	Record << SA_VALUE(TEXT("World"), Value.WorldName);
	Record << SA_VALUE(TEXT("WorldPackage"), Value.WorldPackageName);
}

void operator<<(FStructuredArchive::FSlot Slot, FPersistentStateSlotHeader& Value)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("SlotName"), Value.SlotName);
	Record << SA_VALUE(TEXT("Title"), Value.Title);
	Record << SA_VALUE(TEXT("Timestamp"), Value.Timestamp);
	Record << SA_VALUE(TEXT("LastSavedWorld"), Value.LastSavedWorld);
	Record << SA_VALUE(TEXT("HeaderDataCount"), Value.HeaderDataCount);
}


FPersistentStateSlot::FPersistentStateSlot(FArchive& Ar, const FString& InFilePath)
{
	TrySetFilePath(Ar, InFilePath);
}

FPersistentStateSlot::FPersistentStateSlot(const FName& SlotName, const FText& Title)
{
	SlotHeader.Initialize(SlotName.ToString(), Title);
	GameHeader.InitializeToEmpty();
	bValidSlot = true;
}

bool FPersistentStateSlot::TrySetFilePath(FArchive& Ar, const FString& InFilePath)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(HasFilePath() == false);
	check(Ar.IsLoading() && Ar.Tell() == 0);

	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateLoadFormatter(Ar);
	FStructuredArchive StructuredArchive{*Formatter};
	FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();

	FPersistentStateFixedInteger HeaderTag{INVALID_HEADER_TAG};
	RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
	bValidSlot = HeaderTag == SLOT_HEADER_TAG;
	if (!bValidSlot)
	{
		return false;
	}

	FPersistentStateSlot TempSlot{};
	RootRecord.EnterField(TEXT("SlotHeader")) << TempSlot.SlotHeader;
	RootRecord.EnterField(TEXT("GameHeader")) << TempSlot.GameHeader;

	bValidSlot &= TempSlot.GameHeader.HeaderTag == GAME_HEADER_TAG;
	if (!bValidSlot)
	{
		return false;
	}
	
	TempSlot.GameHeader.CheckValid();
	
	TempSlot.WorldHeaders.Reserve(TempSlot.SlotHeader.HeaderDataCount);
	// read world headers sequentially, abort if any of them happen to be invalid
	for (int32 Count = 0; Count < TempSlot.SlotHeader.HeaderDataCount; ++Count)
	{
		FWorldStateDataHeader TempWorldHeader;
		RootRecord.EnterField(TEXT("WorldHeader")) << TempWorldHeader;

		bValidSlot &= TempWorldHeader.HeaderTag == WORLD_HEADER_TAG;
		if (!bValidSlot)
		{
			return false;
		}
		
		TempWorldHeader.CheckValid();
		TempSlot.WorldHeaders.Add(TempWorldHeader);
	}

	check(bValidSlot);

	*this = TempSlot;
	// ValidBit is false by default, so we should make it true again
	bValidSlot = true;
	FilePath = InFilePath;
	// Rename state slot based if filename is different from the slot name stored in the file
	SlotHeader.SlotName = FPaths::GetBaseFilename(FilePath);
	
	return bValidSlot;
}

void FPersistentStateSlot::SetFilePath(const FString& InFilePath)
{
	FilePath = InFilePath;
}

void FPersistentStateSlot::ResetFileData()
{
	FilePath.Reset();
	SlotHeader.ResetIntermediateData();
	bValidSlot = true;
}

void FPersistentStateSlot::GetStoredWorlds(TArray<FName>& OutStoredWorlds) const
{
	OutStoredWorlds.Reset();
	for (const auto& WorldHeader: WorldHeaders)
	{
		OutStoredWorlds.Add(FName{WorldHeader.WorldName});
	}
}

int32 FPersistentStateSlot::GetWorldHeaderIndex(FName WorldName) const
{
	return WorldHeaders.IndexOfByPredicate([&WorldName](const FWorldStateDataHeader& Header)
	{
		return Header.WorldName == WorldName;
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

	FGameStateSharedRef Result = MakeShared<UE::PersistentState::FGameState>(GameHeader);
	
	if (GameHeader.DataStart > 0)
	{
		TUniquePtr<FArchive> Ar = CreateReadArchive(FilePath);
		check(Ar && Ar->IsLoading());
		
		ReadCompressed(*Ar, GameHeader.DataStart, GameHeader.DataSize, Result->Data);
	}
	
	return Result;
}

void FPersistentStateSlot::SaveStateDirect(FGameStateSharedRef NewGameState, FWorldStateSharedRef NewWorldState, FArchiveFactory CreateWriteArchive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	// verify that slot is associated with file path
	check(bValidSlot && HasFilePath());
	check(NewGameState.IsValid() && NewWorldState.IsValid());

	NewGameState->Header.CheckValid();
	NewWorldState->Header.CheckValid();

	// reset world header information
	WorldHeaders.Empty();

	SaveStateToArchive(NewGameState, NewWorldState, CreateWriteArchive, nullptr);
}

void FPersistentStateSlot::SaveState(const FPersistentStateSlot& SourceSlot, FGameStateSharedRef NewGameState, FWorldStateSharedRef NewWorldState, FArchiveFactory CreateReadArchive, FArchiveFactory CreateWriteArchive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	
	// verify that slot is associated with file path
	check(bValidSlot && HasFilePath());
	check(NewGameState.IsValid() && NewWorldState.IsValid());

	NewGameState->Header.CheckValid();
	NewWorldState->Header.CheckValid();

	// copy world header data from the source slot
	WorldHeaders = SourceSlot.WorldHeaders;	
	// remove old header data for the world, unless it is a new world
	if (const int32 HeaderIndex = GetWorldHeaderIndex(NewWorldState->GetWorld()); WorldHeaders.IsValidIndex(HeaderIndex))
	{
		WorldHeaders.RemoveAtSwap(HeaderIndex);
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
		// read world data from a source slot
		TUniquePtr<FArchive> Reader = CreateReadArchive(SourceSlot.FilePath);
		check(Reader.IsValid() && Reader->IsLoading());

		uint8* PersistentDataPtr = PersistentData.GetData();
		for (const FWorldStateDataHeader& Header: WorldHeaders)
		{
			check(Header.HeaderTag == WORLD_HEADER_TAG);
			Header.CheckValid();
			
			Reader->Seek(Header.DataStart);
			Reader->Serialize(PersistentDataPtr, Header.DataSize);
			PersistentDataPtr += Header.DataSize;
		}
	}

	SaveStateToArchive(NewGameState, NewWorldState, CreateWriteArchive, &PersistentData);
}

void FPersistentStateSlot::SaveStateToArchive(FGameStateSharedRef NewGameState, FWorldStateSharedRef NewWorldState, FArchiveFactory CreateWriteArchive, TArray<uint8>* PersistentData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	// update game and world header data
	GameHeader = NewGameState->Header;
	WorldHeaders.Insert(NewWorldState->Header, 0);
	// game header + world headers
	SlotHeader.HeaderDataCount = WorldHeaders.Num();
	SetLastSavedWorld(NewWorldState->GetWorld());
	
	TUniquePtr<FArchive> WriterArchive = CreateWriteArchive(FilePath);
	check(WriterArchive.IsValid());
		
	FArchive& Writer = *WriterArchive;
	TUniquePtr<FArchiveFormatterType> Formatter = FPersistentStateFormatter::CreateSaveFormatter(Writer);
	FStructuredArchive StructuredArchive{*Formatter};
	FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();
	
	const int32 SlotHeaderTagStart = Writer.Tell();
	{
		FPersistentStateFixedInteger HeaderTag{INVALID_HEADER_TAG};
		// write invalid header tag to identify corrupted save file in case game crashes mid save
		RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
	}
	const int32 SlotHeaderTagEnd = Writer.Tell();

	RootRecord.EnterField(TEXT("SlotHeader")) << SlotHeader;
	const int32 HeaderDataStart = Writer.Tell();
	{
		// write game and world headers. At this point we don't know DataStart, so we will Seek back to @HeaderDataStart
		// to rewrite them
		RootRecord.EnterField(TEXT("GameHeader")) << GameHeader;
		for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
		{
			RootRecord.EnterField(TEXT("WorldHeader")) << WorldHeader;
		}
	}
	
	const int32 HeaderDataEnd = Writer.Tell();
	
	// save new game state
	GameHeader.DataStart = Writer.Tell();
	check(GameHeader.DataSize == NewGameState->Data.Num());
	WriteCompressed(Writer, NewGameState->Data);
	Writer.Serialize(NewGameState->Data.GetData(), NewGameState->Data.Num());
	
	// save new world state
	WorldHeaders[0].DataStart = Writer.Tell();
	check(WorldHeaders[0].DataSize == NewWorldState->Data.Num());
	WriteCompressed(Writer, NewWorldState->Data);
	
	if (WorldHeaders.Num() > 1 && PersistentData != nullptr)
	{
		// Save rest of the worlds. Write old world data, in the same order as it was read.
		// DataSize is the same, DataStart is different.
		uint8* PersistentDataPtr = PersistentData->GetData();
		for (int32 Index = 1; Index < WorldHeaders.Num(); ++Index)
		{
			WorldHeaders[Index].DataStart = Writer.Tell();
			// serialize directly, as data that was read from a source state slot is already in a final state (compressed or not)
			Writer.Serialize(PersistentDataPtr, WorldHeaders[Index].DataSize);

			PersistentDataPtr += WorldHeaders[Index].DataSize;
		}
	}

	// do not re-write header tag if saving with a debug formatter, because we can't safely backtrack with json/xml formatters
	// xml does not write anything to the archive until formatter is destroyed, json is simply scuffed
	// debug formatters are not meant to be read back
	if (FPersistentStateFormatter::IsReleaseFormatter())
	{
		// seek to the header start and re-write game and world headers
		Writer.Seek(HeaderDataStart);
		RootRecord.EnterField(TEXT("GameHeader")) << GameHeader;
		for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
		{
			RootRecord.EnterField(TEXT("WorldHeader")) << WorldHeader;
		}
		check(Writer.Tell() == HeaderDataEnd);
		
		// seek to the start and re-write slot header tag
		Writer.Seek(SlotHeaderTagStart);
		{
			FPersistentStateFixedInteger HeaderTag{SLOT_HEADER_TAG};
			RootRecord << SA_VALUE(TEXT("FileHeaderTag"), HeaderTag);
		}
		check(Writer.Tell() == SlotHeaderTagEnd);
	}
}

FWorldStateSharedRef FPersistentStateSlot::LoadWorldState(FName WorldName, FArchiveFactory CreateReadArchive) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	// verify that slot is associated with file path
	check(HasFilePath());
	check(WorldName != NAME_None);
	
	const int32 HeaderIndex = GetWorldHeaderIndex(WorldName);
	if (!WorldHeaders.IsValidIndex(HeaderIndex))
	{
		// no world data to load. This is OK
		UE_LOG(LogPersistentState, Error, TEXT("%s: Not found world data for world %s in state slot %s. Call HasWorldState beforehand"), *FString(__FUNCTION__), *WorldName.ToString(), *SlotHeader.SlotName);
		return {};
	}

	FWorldStateSharedRef Result = MakeShared<UE::PersistentState::FWorldState>(WorldHeaders[HeaderIndex]);
	if (const FWorldStateDataHeader& Header = WorldHeaders[HeaderIndex]; Header.DataSize > 0)
	{
		TUniquePtr<FArchive> Ar = CreateReadArchive(FilePath);
		check(Ar && Ar->IsLoading());
		
		ReadCompressed(*Ar, Header.DataStart, Header.DataSize, Result->Data);
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

FString FPersistentStateSlot::GetOriginalWorldPackage(FName WorldName) const
{
	const FString WorldNameStr = WorldName.ToString();
	for (const FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		if (WorldHeader.WorldName == WorldNameStr)
		{
			return WorldHeader.WorldPackageName;
		}
	}

	return {};
}
