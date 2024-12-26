#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"
#include "Serialization/VarInt.h"

#include "PersistentStateArchive.generated.h"

class FName;
class FArchive;

/**
 * Helper class to serialize optional property value
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

	uint64 SaveValue(const FSoftObjectPath& Value)
	{
		if (int32* Index = ValueMap.Find(Value))
		{
			check(Values.Contains(Value));
			return *Index;
		}
		
		check(!Values.Contains(Value));
		
		int32 Index = Values.Add(Value);
		ValueMap.Add(Value, Index + 1);
		
		return Index + 1;
	}

	FSoftObjectPath LoadValue(uint64 Index)
	{
		check(Values.IsValidIndex(Index - 1));
		return Values[Index - 1];
	}

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
template <typename TValueType, bool bLoading>
struct PERSISTENTSTATE_API TPersistentStateValueTracker
{
public:
	TPersistentStateValueTracker() = default;
	TPersistentStateValueTracker(const TArray<TValueType>& InValues) requires bLoading
		: Values(InValues)
	{}
	
	uint64 SaveValue(const TValueType& Value)
	{
		check(!bLoading);
		if (int32* Index = ValueMap.Find(Value))
		{
			check(Values.Contains(Value));
			return *Index;
		}
		
		check(!Values.Contains(Value));
		
		int32 Index = Values.Add(Value);
		ValueMap.Add(Value, Index + 1);
		
		return Index + 1;
	}

	TValueType LoadValue(uint64 Index)
	{
		check(bLoading);
		check(Values.IsValidIndex(Index - 1));
		return Values[Index - 1];
	}

	int32 NumValues() const { return Values.Num(); }
	TArrayView<TValueType> GetValues() { return Values; }
	TConstArrayView<TValueType> GetValues() const { return Values; }

	friend FArchive& operator<<(FArchive& Ar, TPersistentStateValueTracker& Tracker)
	{
		Ar << Tracker.Values;
		return Ar;
	}
	
	TArray<TValueType> Values;
private:
	TMap<TValueType, int32> ValueMap;
};

template <bool bLoading>
using FPersistentStateStringTracker	= TPersistentStateValueTracker<FString, bLoading>;

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

template <bool bLoading>
uint32 FPersistentStateStringTrackerProxy<bLoading>::WriteToArchive(FArchive& Ar)
{
	const uint32 StartPosition = Ar.Tell();
	Ar << StringTracker;

	return Ar.Tell() - StartPosition;
}

template <bool bLoading>
void FPersistentStateStringTrackerProxy<bLoading>::ReadFromArchive(FArchive& Ar, int32 StartPosition)
{
	const int32 CurrentPosition = Ar.Tell();
	Ar.Seek(StartPosition);
	
	Ar << StringTracker;
	
	Ar.Seek(CurrentPosition);
}

template <bool bLoading>
FArchive& FPersistentStateStringTrackerProxy<bLoading>::operator<<(FName& Name)
{
	if constexpr (bLoading)
	{
		const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
		check(Index != 0);

		FString Str = StringTracker.LoadValue(Index);
		Name = FName{Str};
	}
	else
	{
		const uint64 Index = StringTracker.SaveValue(Name.ToString());
		check(Index != 0);
		
		WriteVarUIntToArchive(InnerArchive, Index);
	}

	return *this;
}

enum EObjectDependency: uint8
{
	Soft = 1,
	Hard = 2,
	All = 255,
};

/**
 * Proxy for top level asset and soft object tracker
 * Responsible for gathering soft objects and top level assets during serialization
 * ObjectTrackerProxy should wrap StringTrackerProxy to optimize space for object path serialization
 */
template <bool bLoading, EObjectDependency DependencyMode>
struct FPersistentStateObjectTrackerProxy: public FArchiveProxy
{
	FPersistentStateObjectTrackerProxy(FArchive& InArchive, FPersistentStateObjectTracker& InObjectTracker)
		: FArchiveProxy(InArchive)
		, ObjectTracker(InObjectTracker)
	{}

	/** write object tracker contents to underlying archive */
	uint32 WriteToArchive(FArchive& Ar);
	/** read object tracker contents from underlying archive */
	void ReadFromArchive(FArchive& Ar, int32 StartPosition);
	
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	
	FPersistentStateObjectTracker& ObjectTracker;
};

template <bool bLoading, EObjectDependency DependencyMode>
uint32 FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::WriteToArchive(FArchive& Ar)
{
	if constexpr (!bLoading)
	{
		const uint32 StartPosition = Ar.Tell();

		int32 Num = ObjectTracker.NumValues();
		Ar << Num;

		// soft object paths are serialized as string, so they can be catched by a string tracker
		for (FSoftObjectPath& Obj: ObjectTracker.GetValues())
		{
			Obj.SerializePath(Ar);
		}
	
		return Ar.Tell() - StartPosition;
	}
	else
	{
		return 0;
	}
}

template <bool bLoading, EObjectDependency DependencyMode>
void FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::ReadFromArchive(FArchive& Ar, int32 StartPosition)
{
	if constexpr (bLoading)
	{
		const int32 CurrentPosition = Ar.Tell();
		Ar.Seek(StartPosition);
	
		int32 Num{};
		Ar << Num;

		ObjectTracker.Values.SetNum(Num);
		for (FSoftObjectPath& Obj: ObjectTracker.Values)
		{
			Obj.SerializePath(Ar);
		}

		Ar.Seek(CurrentPosition);
	}
}

template <bool bLoading, EObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(UObject*& Obj)
{
	if constexpr (DependencyMode & EObjectDependency::Hard)
	{
		if constexpr (bLoading)
		{
			// if this is 0, then it wasn't a class
			if (const uint64 Index = ReadVarUIntFromArchive(InnerArchive); Index != 0)
			{
				FSoftObjectPath ObjectPath = ObjectTracker.LoadValue(Index);
				check(ObjectPath.IsValid());
			
				UObject* Object = ObjectPath.ResolveObject();
				// @todo: this check will fail if we're trying to load deleted object
				check(Object);

				Obj = Object;
			}
			else
			{
				InnerArchive << Obj;
			}
		}
		else
		{
			if (UObject* Object = Obj; Object && FAssetData::IsTopLevelAsset(Object))
			{
				uint64 ObjectIndex = ObjectTracker.SaveValue(FSoftObjectPath{Object});
				check(ObjectIndex != 0);
			
				WriteVarUIntToArchive(InnerArchive, ObjectIndex);
			}
			else
			{
				// write
				WriteVarUIntToArchive(InnerArchive, 0ULL);
				InnerArchive << Obj;
			}
		}
	}
	else
	{
		InnerArchive << Obj;
	}

	return *this;
}

template <bool bLoading, EObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FObjectPtr& Obj)
{
	// route serialization to UObject*&
	return FArchiveUObject::SerializeObjectPtr(*this, Obj);
}

template <bool bLoading, EObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FSoftObjectPtr& Value)
{
	if constexpr (DependencyMode & EObjectDependency::Soft)
	{
		if constexpr (bLoading)
		{
			const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
			check(Index != 0);

			Value = ObjectTracker.LoadValue(Index);
		}
		else
		{
			uint64 ObjectIndex = ObjectTracker.SaveValue(Value.GetUniqueID());
			check(ObjectIndex != 0);

			WriteVarUIntToArchive(InnerArchive, ObjectIndex);
		}
	}
	else
	{
		InnerArchive << Value;
	}

	return *this;
}

template <bool bLoading, EObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FSoftObjectPath& Value)
{
	if constexpr (DependencyMode & EObjectDependency::Soft)
	{
		if constexpr (bLoading)
		{
			const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
			check(Index != 0);
			
			Value = ObjectTracker.LoadValue(Index);
		}
		else
		{
			uint64 ObjectIndex = ObjectTracker.SaveValue(Value);
			check(ObjectIndex != 0);

			WriteVarUIntToArchive(InnerArchive, ObjectIndex);
		}
	}
	else
	{
		InnerArchive << Value;
	}

	return *this;
}

