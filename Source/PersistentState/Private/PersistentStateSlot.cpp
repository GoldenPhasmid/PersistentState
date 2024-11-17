#include "PersistentStateSlot.h"

#include "PersistentStateDefines.h"

FPersistentStateSlot::FPersistentStateSlot(FArchive& Ar, const FString& InFilePath, const FString& InFileName)
	: FilePath(InFilePath)
	, FileName(InFileName)
{
	check(Ar.IsLoading());
	check(Ar.Tell() == 0);
	
	Ar << Header;
	bValidBit = Header.SlotHeaderTag != SLOT_HEADER_TAG;

	const int32 Position = Ar.Tell();
	Ar.Seek(Position + Header.WorldDataOffset);
	
	WorldHeaders.Reserve(Header.WorldCount);
	for (int32 Count = 0; Count < Header.WorldCount; ++Count)
	{
		const int32 WorldPosition = Ar.Tell();
		FWorldStateDataHeader WorldHeader{};
		Ar << WorldHeader;
		
		WorldHeader.WorldDataPosition = WorldPosition;
		WorldHeader.CheckValid();
		
		WorldHeaders.Add(WorldHeader);

		Ar.Seek(WorldPosition + WorldHeader.WorldDataSize);
	}
}

void FPersistentStateSlot::UpdateWorldHeader(const FWorldStateDataHeader& NewWorldHeader)
{
	for (FWorldStateDataHeader& WorldHeader: WorldHeaders)
	{
		// existing world is saved, update its data to properly load it again
		if (WorldHeader.WorldName == NewWorldHeader.WorldName)
		{
			WorldHeader = NewWorldHeader;
			return;
		}
	}

	// new world is saved
	WorldHeaders.Add(NewWorldHeader);
}

FWorldStateSharedRef FPersistentStateSlot::LoadWorldState(FArchive& Ar, FName WorldName) const
{
	check(WorldName != NAME_None);
	check(Ar.IsLoading());

	int32 Index = 0;
	for (; Index < WorldHeaders.Num(); ++Index)
	{
		if (WorldName == WorldHeaders[Index].WorldName)
		{
			break;
		}
	}

	if (Index == WorldHeaders.Num())
	{
		// no world data to load. This is OK
		UE_LOG(LogPersistentState, Log, TEXT("%s: Not found world data for world %s in state slot %s"), *FString(__FUNCTION__), *WorldName.ToString(), *Header.SlotName);
		return {};
	}

	Ar.Seek(WorldHeaders[Index].WorldDataPosition);

	FWorldStateDataHeader WorldHeader{};
	Ar << WorldHeader;
	WorldHeader.CheckValid();

	if (WorldHeader.WorldHeaderTag != WORLD_HEADER_TAG)
	{
		UE_LOG(LogPersistentState, Fatal, TEXT("%s: Corrupted state slot %s for world %s"), *FString(__FUNCTION__), *WorldName.ToString(), *Header.SlotName);
		return {};
	}
	check(WorldHeader.WorldName == WorldName);
	
	FWorldStateSharedRef Result = MakeShared<UE::PersistentState::FWorldState>(WorldName);
	Result->Data.SetNum(WorldHeader.WorldDataSize);
	
	Ar.Serialize(Result->GetData().GetData(), WorldHeader.WorldDataSize);

	check(!Result->GetData().IsEmpty());
	return Result;
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
