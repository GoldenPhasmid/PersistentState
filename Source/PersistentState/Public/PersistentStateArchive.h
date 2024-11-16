#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

class FName;
class FArchive;

template <typename TReferenceType>
struct PERSISTENTSTATE_API TPersistentStateTracker
{
public:

	uint64 TrackReference(const TReferenceType& Reference)
	{
		if (int32* Index = ObjectMap.Find(Reference))
		{
			return *Index;
		}
		
		int32 Index = Objects.Add(Reference);
		ObjectMap.Add(Reference, Index + 1);

		return Index + 1;
	}

	TReferenceType RestoreReference(uint64 Index)
	{
		check(Objects.IsValidIndex(Index - 1));
		return Objects[Index - 1];
	}
	
	TArray<TReferenceType> Objects;
	TMap<TReferenceType, int32> ObjectMap;
};

using FPersistentStateObjectTracker = TPersistentStateTracker<FSoftObjectPath>;
using FPersistentStateStringTracker	= TPersistentStateTracker<FString>;

struct PERSISTENTSTATE_API FPersistentStateStringTrackerProxy: public FArchiveProxy
{
	FPersistentStateStringTrackerProxy(FArchive& InArchive)
		: FArchiveProxy(InArchive)
	{}
	
	uint32 WriteToArchive(FArchive& Ar);
	void ReadFromArchive(FArchive& Ar, int32 StartPosition);

	virtual FArchive& operator<<(FName& Name) override;
	
	FPersistentStateStringTracker StringTracker;
};

struct PERSISTENTSTATE_API FPersistentStateObjectTrackerProxy: public FArchiveProxy
{
	FPersistentStateObjectTrackerProxy(FArchive& InArchive)
		: FArchiveProxy(InArchive)
	{}
	
	uint32 WriteToArchive(FArchive& Ar);
	void ReadFromArchive(FArchive& Ar, int32 StartPosition);
	
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	
	FPersistentStateObjectTracker ObjectTracker;
};

/** persistent state archive */
struct PERSISTENTSTATE_API FPersistentStateProxyArchive: public FArchiveProxy
{
	FPersistentStateProxyArchive(FArchive& InArchive)
		: FArchiveProxy(InArchive)
	{
	}

	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FLazyObjectPtr& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
};

/** persistent state archive */
struct PERSISTENTSTATE_API FPersistentStateSaveGameArchive: public FPersistentStateProxyArchive
{
	FPersistentStateSaveGameArchive(FArchive& InArchive)
		: FPersistentStateProxyArchive(InArchive)
	{
	}

	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(FLazyObjectPtr& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
};


class FPersistentStateMemoryReader: public FMemoryReader
{
public:
	FPersistentStateMemoryReader(const TArray<uint8>& InBytes, bool bIsPersistent = false)
		: FMemoryReader(InBytes, bIsPersistent)
	{}
};

class FPersistentStateMemoryWriter: public FMemoryWriter
{
public:
	using FMemoryWriter::FMemoryWriter;
};


