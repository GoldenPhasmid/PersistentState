#include "PersistentStateSlot.h"

#include "PersistentStateModule.h"

FPersistentStateSlot::FPersistentStateSlot(FArchive& Ar, const FString& InFilePath)
{
	TrySetFilePath(Ar, InFilePath);
}

FPersistentStateSlot::FPersistentStateSlot(const FName& SlotName, const FText& Title)
{
	SlotHeader.Initialize(SlotName.ToString(), Title);
	GameHeader.InitializeToEmpty();
	bValidBit = true;
}

bool FPersistentStateSlot::TrySetFilePath(FArchive& Ar, const FString& InFilePath)
{
	check(HasFilePath() == false);
	check(Ar.IsLoading() && Ar.Tell() == 0);

	int32 HeaderTag{INVALID_HEADER_TAG};
	Ar << HeaderTag;
	
	bValidBit = true;
	bValidBit &= HeaderTag == SLOT_HEADER_TAG;
	
	if (!bValidBit)
	{
		return false;
	}
	
	FPersistentStateSlotHeader DummyHeader;
	Ar << DummyHeader;
	
	// read game header
	FGameStateDataHeader GameDummyHeader;
	Ar << GameDummyHeader;
	bValidBit &= GameDummyHeader.HeaderTag == GAME_HEADER_TAG;
	if (!bValidBit)
	{
		return false;
	}
	
	GameDummyHeader.CheckValid();
	
	TArray<FWorldStateDataHeader, TInlineAllocator<4>> Headers;
	Headers.Reserve(DummyHeader.HeaderDataCount);
	
	// read world headers sequentially, abort if any of them happen to be invalid
	for (uint32 Count = 0; Count < DummyHeader.HeaderDataCount; ++Count)
	{
		FWorldStateDataHeader& Header = Headers.Emplace_GetRef();
		Ar << Header;

		bValidBit &= Header.HeaderTag == WORLD_HEADER_TAG;
		if (!bValidBit)
		{
			return false;
		}
		
		Header.CheckValid();
	}

	if (bValidBit)
	{
		FilePath = InFilePath;
		SlotHeader = DummyHeader;
		GameHeader = GameDummyHeader;
		WorldHeaders = Headers;
	}

	return bValidBit;
}

void FPersistentStateSlot::SetFilePath(const FString& InFilePath)
{
	FilePath = InFilePath;
}

void FPersistentStateSlot::ResetFileData()
{
	FilePath.Reset();
	SlotHeader.ResetIntermediateData();
	bValidBit = true;
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
	// verify that slot is associated with file path
	check(HasFilePath());

	FGameStateSharedRef Result = MakeShared<UE::PersistentState::FGameState>(GameHeader);
	Result->Data.Reserve(GameHeader.DataSize + 2);
	
	if (GameHeader.DataStart > 0)
	{
		TUniquePtr<FArchive> Ar = CreateReadArchive(FilePath);
		check(Ar && Ar->IsLoading());
		FArchive& Reader = *Ar;
		
		Reader.Seek(GameHeader.DataStart);
		Reader.Serialize(Result->Data.GetData(), GameHeader.DataSize);
	}
	
	return Result;
}

FWorldStateSharedRef FPersistentStateSlot::LoadWorldState(FName WorldName, FArchiveFactory CreateReadArchive) const
{
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

	Result->Data.Reserve(WorldHeaders[HeaderIndex].DataSize + 2);

	TUniquePtr<FArchive> Ar = CreateReadArchive(FilePath);
	check(Ar && Ar->IsLoading());

	FArchive& Reader = *Ar;
	LoadWorldData(WorldHeaders[HeaderIndex], Reader, Result->Data);
	
	return Result;
}

void FPersistentStateSlot::LoadWorldData(const FWorldStateDataHeader& Header, FArchive& Reader, TArray<uint8>& OutData)
{
	check(Header.HeaderTag == WORLD_HEADER_TAG);
	Header.CheckValid();

	if (Header.DataSize > 0)
	{
		Reader.Seek(Header.DataStart);
		Reader.Serialize(OutData.GetData(), Header.DataSize);
		check(!OutData.IsEmpty());
	}
}

bool FPersistentStateSlot::SaveState(const FPersistentStateSlot& SourceSlot, FGameStateSharedRef NewGameState, FWorldStateSharedRef NewWorldState, FArchiveFactory CreateReadArchive, FArchiveFactory CreateWriteArchive)
{
	// verify that slot is associated with file path
	check(bValidBit && HasFilePath());
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
		
		PersistentData.Reserve(PersistentDataSize);
		// read world data from a source slot
		TUniquePtr<FArchive> Reader = CreateReadArchive(SourceSlot.FilePath);
		check(Reader.IsValid() && Reader->IsLoading());
			
		for (int32 Index = 0; Index < WorldHeaders.Num(); ++Index)
		{
			LoadWorldData(WorldHeaders[Index], *Reader, PersistentData);
		}
	}

	// update game and world header data
	GameHeader = NewGameState->Header;
	WorldHeaders.Insert(NewWorldState->Header, 0);
	// game header + world headers
	SlotHeader.HeaderDataCount = WorldHeaders.Num();
	SetLastSavedWorld(NewWorldState->GetWorld());
	
	TUniquePtr<FArchive> WriterArchive = CreateWriteArchive(FilePath);
	check(WriterArchive.IsValid());
		
	FArchive& Writer = *WriterArchive;

	const int32 SlotHeaderTagStart = Writer.Tell();
	check(SlotHeaderTagStart == 0);
	int32 HeaderTag{INVALID_HEADER_TAG};
	// write invalid header tag to identify corrupted save file in case game crashes mid save
	Writer << HeaderTag;
	const int32 SlotHeaderTagEnd = Writer.Tell();

	Writer << SlotHeader;
	const int32 HeaderDataStart = Writer.Tell();

	// write game and world headers. At this point we don't know DataStart, so we will Seek back to @HeaderDataStart
	// to rewrite them
	Writer << GameHeader;
	for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		Writer << WorldHeader;
	}

	const int32 HeaderDataEnd = Writer.Tell();

	// save new game state
	GameHeader.DataStart = Writer.Tell();
	check(GameHeader.DataSize == NewGameState->Data.Num());
	Writer.Serialize(NewGameState->Data.GetData(), NewGameState->Data.Num());

	// save new world state
	WorldHeaders[0].DataStart = Writer.Tell();
	check(WorldHeaders[0].DataSize == NewWorldState->Data.Num());
	Writer.Serialize(NewWorldState->Data.GetData(), NewWorldState->Data.Num());
	
	if (WorldHeaders.Num() > 1)
	{
		// Save rest of the worlds. Write old world data, in the same order as it was read.
		// DataSize is the same, DataStart is different.
		uint8* PersistentDataPtr = PersistentData.GetData();
		for (int32 Index = 1; Index < WorldHeaders.Num(); ++Index)
		{
			WorldHeaders[Index].DataStart = Writer.Tell();
			Writer.Serialize(PersistentDataPtr, WorldHeaders[Index].DataSize);

			PersistentDataPtr += WorldHeaders[Index].DataSize;
		}
	}

	// seek to the header start and re-write game and world headers
	Writer.Seek(HeaderDataStart);
	Writer << GameHeader;
	for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		Writer << WorldHeader;
	}
	check(Writer.Tell() == HeaderDataEnd);
		
	// seek to the start and re-write slot header tag
	Writer.Seek(SlotHeaderTagStart);
	HeaderTag = SLOT_HEADER_TAG;
	Writer << HeaderTag;
	check(Writer.Tell() == SlotHeaderTagEnd);
	
	return true;
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
