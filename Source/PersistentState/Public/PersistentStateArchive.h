#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"


#include "PersistentStateArchive.generated.h"

class FName;
class FArchive;

/**
 * Helper class to delta serialize property values.
 * Example: Ar << TDeltaSerializeHelper{MyInt, ShouldSerializeInt};
 * MyInt is serialized only if ShouldSerializeInt == true
 */
template <typename TPropertyType>
struct TDeltaSerializeHelper
{
	TDeltaSerializeHelper(TPropertyType& InValue, bool bInShouldSerialize)
		: Value(InValue)
		, bShouldSerialize(bInShouldSerialize)
	{}

	friend FArchive& operator<<(FArchive& Ar, const TDeltaSerializeHelper<TPropertyType>& Delta)
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

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateObjectTracker
{
	GENERATED_BODY()
public:

	/** Map object path to an index which caller is expected to serialize instead of a string */
	uint64 SaveValue(const FSoftObjectPath& Value);
	/** Map deserialized object path index to a full object path */
	FSoftObjectPath LoadValue(uint64 Index);

	void Reset()
	{
		Values.Reset();
		ValueMap.Reset();
	}

	bool IsEmpty() const
	{
		return Values.IsEmpty();
	}

	int32 NumValues() const { return Values.Num(); }
	TArrayView<FSoftObjectPath> GetValues() { return Values; }
	TConstArrayView<FSoftObjectPath> GetValues() const { return Values; }
	
	// @todo: make private
	UPROPERTY()
	TArray<FSoftObjectPath> Values;
private:
	TMap<FSoftObjectPath, int32> ValueMap;
};


/**
 * Generic implementation for reference tracker of a specific type
 */
template <bool bLoading>
struct PERSISTENTSTATE_API FPersistentStateStringTracker
{
public:
	FPersistentStateStringTracker() = default;
	FPersistentStateStringTracker(const TArray<FString>& InValues) requires bLoading
		: Values(InValues)
	{}

	/** Map string to an index which caller is expected to serialize instead of a string */
	uint64 SaveValue(const FString& Value) requires !bLoading;
	/** Map deserialized string index to a full string */
	FString LoadValue(uint64 Index) requires bLoading;

	int32 NumValues() const { return Values.Num(); }
	TArrayView<FString> GetValues() { return Values; }
	TConstArrayView<FString> GetValues() const { return Values; }
	
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateStringTracker& Tracker)
	{
		Ar << Tracker.Values;
		return Ar;
	}
	
	TArray<FString> Values;
private:
	TMap<FString, int32> ValueMap;
};

/**
 * Proxy for string tracker, responsible for compact FName serialization
 * StringTrackerProxy should wrap an archive or another proxy to track secondary serialization
 */
template <bool bLoading>
struct FPersistentStateStringTrackerProxy: public FArchiveProxy
{
	FPersistentStateStringTrackerProxy(FArchive& InArchive)
		: FArchiveProxy(InArchive)
	{}
	
	uint32 WriteToArchive(FArchive& Ar);
	void ReadFromArchive(FArchive& Ar, int32 StartPosition);

	virtual FArchive& operator<<(FName& Name) override;
	
	FPersistentStateStringTracker<bLoading> StringTracker;
};

/**
 * Enum that defines which dependency types are serialized as indexes via object tracker proxy
 * Soft - only soft dependencies
 * Hard - only hard dependencies
 * All - soft AND hard dependencies
 */
enum ESerializeObjectDependency: uint8
{
	Soft = 1,
	Hard = 2,
	All = 255,
};

/**
 * Proxy for top level asset and soft object tracker. Responsible for gathering soft objects and top level assets during serialization
 * Can be used to wrap "String Tracker", so that soft object paths are indirected further via string table
 * ObjectTrackerProxy should be initialized with the same template arguments for both save and load to operate properly
 */
template <bool bLoading, ESerializeObjectDependency DependencyMode>
struct FPersistentStateObjectTrackerProxy: public FArchiveProxy
{
	FPersistentStateObjectTrackerProxy(FArchive& InArchive, FPersistentStateObjectTracker& InObjectTracker)
		: FArchiveProxy(InArchive)
		, ObjectTracker(InObjectTracker)
	{}

	/** write object tracker contents to underlying archive */
	uint32 WriteToArchive(FArchive& Ar) const;
	/** read object tracker contents from underlying archive */
	void ReadFromArchive(FArchive& Ar, int32 StartPosition) const;
	
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	
	FPersistentStateObjectTracker& ObjectTracker;
};
