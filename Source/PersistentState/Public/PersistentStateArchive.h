#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

class FName;
class FArchive;

/**
 * Helper class to serialize optional property value
 */
template <typename TPropertyType>
struct TDeltaSerialize
{
	TDeltaSerialize(TPropertyType& InValue, bool bInShouldSerialize)
		: Value(InValue)
		, bShouldSerialize(bInShouldSerialize)
	{}

	friend FArchive& operator<<(FArchive& Ar, const TDeltaSerialize<TPropertyType>& Delta)
	{
		if (Delta.bShouldSerialize)
		{
			Ar << Delta.Value;
		}
		
		return Ar;
	}

	TPropertyType& Value;
	bool bShouldSerialize;
};


/**
 * Generic implementation for reference tracker of a specific type
 */
template <typename TReferenceType>
struct PERSISTENTSTATE_API TPersistentStateReferenceTracker
{
public:
	TPersistentStateReferenceTracker() = default;
	TPersistentStateReferenceTracker(TConstArrayView<TReferenceType> InReferences)
		: References(InReferences)
	{}
	
	uint64 TrackReference(const TReferenceType& Reference)
	{
		if (int32* Index = ReferenceMap.Find(Reference))
		{
			check(References.Contains(Reference));
			return *Index;
		}
		
		check(!References.Contains(Reference));
		
		int32 Index = References.Add(Reference);
		ReferenceMap.Add(Reference, Index + 1);

		return Index + 1;
	}

	TReferenceType RestoreReference(uint64 Index)
	{
		check(References.IsValidIndex(Index - 1));
		return References[Index - 1];
	}

	int32 NumReferences() const { return References.Num(); }
	TArrayView<TReferenceType> GetReferences() { return References; }
	TConstArrayView<TReferenceType> GetReferences() const { return References; }

	friend FArchive& operator<<(FArchive& Ar, TPersistentStateReferenceTracker& Tracker)
	{
		Ar << Tracker.References;
		return Ar;
	}
	
	TArray<TReferenceType> References;
private:
	TMap<TReferenceType, int32> ReferenceMap;
};

using FPersistentStateObjectTracker = TPersistentStateReferenceTracker<FSoftObjectPath>;
using FPersistentStateStringTracker	= TPersistentStateReferenceTracker<FString>;

/**
 * Proxy for string tracker, responsible for compact FName serialization
 * StringTrackerProxy should be double proxied ObjectTrackerProxy to optimize space for object path serialization
 */
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

enum EObjectDependency: uint8
{
	Soft = 1,
	Hard = 2,
	All = 255,
};

/**
 * Proxy for soft object tracker, responsible for gathering soft objects and top level assets during serialization
 */
struct PERSISTENTSTATE_API FPersistentStateObjectTrackerProxy: public FArchiveProxy
{
	FPersistentStateObjectTrackerProxy(FArchive& InArchive, FPersistentStateObjectTracker& InObjectTracker, EObjectDependency InDependencyMode = EObjectDependency::All)
		: FArchiveProxy(InArchive)
		, ObjectTracker(InObjectTracker)
		, DependencyMode(InDependencyMode)
	{}
	
	uint32 WriteToArchive(FArchive& Ar);
	void ReadFromArchive(FArchive& Ar, int32 StartPosition);
	
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	
	FPersistentStateObjectTracker& ObjectTracker;
	/** dependency mode, should be the same when saving and loading */
	EObjectDependency DependencyMode;
};

/** persistent state archive
 * 
 */
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
	virtual FArchive& operator<<(UObject*& Obj) override;
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


