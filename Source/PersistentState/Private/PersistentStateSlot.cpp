#include "PersistentStateSlot.h"

#include "PersistentStateDefines.h"

FPersistentStateSlot::FPersistentStateSlot(FArchive& Ar, const FString& InFilePath)
{
	TrySetFilePath(Ar, InFilePath);
}

FPersistentStateSlot::FPersistentStateSlot(const FString& SlotName, const FText& Title)
{
	Header.SlotName = SlotName;
	Header.Title = Title;
	bValidBit = true;
}

bool FPersistentStateSlot::TrySetFilePath(FArchive& Ar, const FString& InFilePath)
{
	check(HasFilePath() == false);
	check(Ar.IsLoading() && Ar.Tell() == 0);
	
	Ar << Header;
	bValidBit = Header.SlotHeaderTag != SLOT_HEADER_TAG;
	
	if (!bValidBit)
	{
		return false;
	}

	FilePath = InFilePath;
	Ar.Seek(Header.WorldHeaderDataStart);
	
	WorldHeaders.Reserve(Header.WorldHeaderDataCount);
	// read world headers sequentially, abort if any of them happen to be invalid
	for (uint32 Count = 0; Count < Header.WorldHeaderDataCount; ++Count)
	{
		FWorldStateDataHeader& WorldHeader = WorldHeaders.Emplace_GetRef();
		Ar << WorldHeader;
		
		if (WorldHeader.WorldHeaderTag != WORLD_HEADER_TAG)
		{
			bValidBit = false;
			return false;
		}
		
		WorldHeader.CheckValid();
	}

	return bValidBit;
}

void FPersistentStateSlot::SetFilePath(const FString& InFilePath)
{
	FilePath = InFilePath;
}

void FPersistentStateSlot::ResetFileData()
{
	FilePath = {};
	Header.ResetIntermediateData();
	bValidBit = true;
}

FWorldStateSharedRef FPersistentStateSlot::LoadWorldState(FArchive& ReadArchive, FName WorldName) const
{
	// verify that slot is associated with file path
	check(HasFilePath());
	check(WorldName != NAME_None && ReadArchive.IsLoading());

	const int32 HeaderIndex = WorldHeaders.IndexOfByPredicate([&WorldName](const FWorldStateDataHeader& Header)
	{
		return Header.WorldName == WorldName;
	});

	if (HeaderIndex == WorldHeaders.Num())
	{
		// no world data to load. This is OK
		UE_LOG(LogPersistentState, Log, TEXT("%s: Not found world data for world %s in state slot %s"), *FString(__FUNCTION__), *WorldName.ToString(), *Header.SlotName);
		return {};
	}

	FWorldStateSharedRef Result = MakeShared<UE::PersistentState::FWorldState>(WorldHeaders[HeaderIndex]);

	Result->Data.AddUninitialized(WorldHeaders[HeaderIndex].WorldDataSize + 2);
	LoadWorldData(ReadArchive, HeaderIndex, Result->Data);
	
	return Result;
}

void FPersistentStateSlot::LoadWorldData(FArchive& ReadArchive, int32 HeaderIndex, TArray<uint8>& OutData) const
{
	check(WorldHeaders.IsValidIndex(HeaderIndex));
	
	const FWorldStateDataHeader& WorldHeader = WorldHeaders[HeaderIndex];
	WorldHeader.CheckValid();
	UE_CLOG(WorldHeader.WorldHeaderTag != WORLD_HEADER_TAG, LogPersistentState, Fatal, TEXT("%s: Corrupted state slot %s for world %s"), *FString(__FUNCTION__), *WorldHeaders[HeaderIndex].WorldName, *Header.SlotName);

	ReadArchive.Seek(WorldHeaders[HeaderIndex].WorldDataStart);
	ReadArchive.Serialize(OutData.GetData(), WorldHeader.WorldDataSize);
	check(!OutData.IsEmpty());
}

bool FPersistentStateSlot::SaveWorldState(FWorldStateSharedRef NewWorldState, TFunction<TUniquePtr<FArchive>(const FString&)> CreateReadArchive, TFunction<TUniquePtr<FArchive>(const FString&)> CreateWriteArchive)
{
	// verify that slot is associated with file path
	check(bValidBit && HasFilePath() && NewWorldState.IsValid() && NewWorldState);

	{
		const int32 HeaderIndex = WorldHeaders.IndexOfByPredicate([WorldName=NewWorldState->GetWorld()](const FWorldStateDataHeader& Header)
		{
			return Header.WorldName == WorldName;
		});
		// remove old header data for the world, unless it is a new world
		if (HeaderIndex != INDEX_NONE)
		{
			WorldHeaders.RemoveAtSwap(HeaderIndex);
		}
	}

	int32 TotalSize = 0;
	// @todo: sort new headers by WorldDataStart to access reader sequentially
	for (const FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		TotalSize += WorldHeader.WorldDataSize;
	}
	
	TArray<uint8> OldWorldData;
	OldWorldData.AddUninitialized(TotalSize);
	{
		// read world data
		TUniquePtr<FArchive> Reader = CreateReadArchive(FilePath);
		check(Reader.IsValid());

		// @todo: serialize slot persistent data, e.g. player info, meta progression
		for (int32 Index = 0; Index < WorldHeaders.Num(); ++Index)
		{
			LoadWorldData(*Reader, Index, OldWorldData);
		}
	}

	WorldHeaders.Insert(NewWorldState->Header, 0);
	SetLastSavedWorld(NewWorldState->GetWorld());
	
	{
		TUniquePtr<FArchive> WriterArchive = CreateWriteArchive(FilePath);
		check(WriterArchive.IsValid());
		
		FArchive& Writer = *WriterArchive;

		const int32 DataStart = Writer.Tell();
		check(DataStart == 0);
		
		Writer << Header;
		
		Header.WorldHeaderDataStart = Writer.Tell();
		Header.WorldHeaderDataCount = WorldHeaders.Num();
		for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
		{
			Writer << WorldHeader;
		}

		WorldHeaders[0].WorldDataStart = Writer.Tell();
		check(WorldHeaders[0].WorldDataSize == NewWorldState->Data.Num());
		Writer.Serialize(NewWorldState->Data.GetData(), NewWorldState->Data.Num());

		// @todo: serialize slot persistent data, e.g. player info, meta progression
		uint8* OldWorldDataPtr = OldWorldData.GetData();
		for (int32 Index = 1; Index < WorldHeaders.Num(); ++Index)
		{
			WorldHeaders[Index].WorldDataStart = Writer.Tell();
			Writer.Serialize(OldWorldDataPtr, WorldHeaders[Index].WorldDataSize);

			OldWorldDataPtr += WorldHeaders[Index].WorldDataSize;
		}

		// seek to the start and re-write slot header
		Writer.Seek(DataStart);
		Writer << Header;

		// seek to the header start and re-write world headers
		Writer.Seek(Header.WorldHeaderDataStart);
		for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
		{
			Writer << WorldHeader;
		}
	}

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
